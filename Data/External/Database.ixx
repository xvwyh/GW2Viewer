module;
#include <sqlite_modern_cpp.h>

export module GW2Viewer.Data.External.Database;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import GW2Viewer.Content.Conversation;
import GW2Viewer.Content.Event;
import GW2Viewer.Data.Encryption.Asset;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.ConversationListViewer;
import GW2Viewer.UI.Viewers.EventListViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Viewers.StringListViewer;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Async.ProgressBarContext;
import GW2Viewer.Utils.Enum;
import std;
import <gsl/gsl>;

using namespace std::chrono_literals;

export namespace GW2Viewer::Data::External
{

struct LoadingOperation
{
    struct Options
    {
        std::string Joins = "";
        std::string Condition = "1";
        std::shared_mutex* SharedMutex = nullptr;
        std::function<void()> PostHandler = nullptr;
    };

    virtual ~LoadingOperation() = default;
    virtual void Process(sqlite::database& db) = 0;
    virtual void PostProcess() = 0;

    static auto Make(std::string_view table, std::string_view columns, auto&& handler, Options&& options = { });
};
template<typename... Args>
struct LoadingOperationT : LoadingOperation
{
    std::string Table;
    std::string Columns;
    std::function<void(Args...)> Handler;
    Options Options;
    std::string Query = std::format("SELECT {}._rowid_, {} FROM {} {} WHERE {}._rowid_ > ? AND ({})", Table, Columns, Table, Options.Joins, Table, Options.Condition);
    sqlite_int64 MaxRowID = -1;
    sqlite_int64 LastMaxRowID = -1;

    LoadingOperationT(std::string_view table, std::string_view columns, std::function<void(Args...)>&& handler, LoadingOperation::Options&& options) : Table(table), Columns(columns), Handler(std::move(handler)), Options(std::move(options)) { }

    template<typename T> struct CoerceArgumentType { using Type = T; };
    template<> struct CoerceArgumentType<uint64> { using Type = sqlite_uint64; };
    template<> struct CoerceArgumentType<int64> { using Type = sqlite_int64; };
    template<> struct CoerceArgumentType<GUID> { using Type = std::vector<byte>; };
    template<Enumeration Enum> struct CoerceArgumentType<Enum> { using Type = std::underlying_type_t<Enum>; };

    void Process(sqlite::database& db) override
    {
        if (Options.SharedMutex)
            Options.SharedMutex->lock();

        auto _ = gsl::finally([this]
        {
            if (Options.SharedMutex)
                Options.SharedMutex->unlock();
        });

        db << Query << MaxRowID >> [this](sqlite_int64 rowID, typename CoerceArgumentType<std::decay_t<Args>>::Type... args)
        {
            Handler(((Args)args)...);
            MaxRowID = std::max(MaxRowID, rowID);
        };

    }
    void PostProcess() override
    {
        if (LastMaxRowID != MaxRowID)
        {
            LastMaxRowID = MaxRowID;
            if (Options.PostHandler)
                G::UI.Defer(std::function(Options.PostHandler));
        }
    }
};
auto LoadingOperation::Make(std::string_view table, std::string_view columns, auto&& handler, Options&& options)
{
    return std::unique_ptr<LoadingOperation>(new LoadingOperationT(table, columns, std::function(handler), std::move(options)));
}

class Database
{
public:
    void Load(std::filesystem::path const& path, Utils::Async::ProgressBarContext& progress)
    {
        namespace Content = GW2Viewer::Content;

        auto updateConversationSearch = [] { G::Viewers::Notify(&UI::Viewers::ConversationListViewer::UpdateSearch); };
        auto updateEventFilter = [] { G::Viewers::Notify(&UI::Viewers::EventListViewer::UpdateFilter); };

        using namespace sqlite;
        progress.Start("Reading string decryption keys");
        static database db(path.u16string(), { .flags = OpenFlags::READONLY });
        static std::unique_ptr<LoadingOperation> operations[]
        {
            LoadingOperation::Make("Texts",
                "TextID, Key, Time, Session, Map, ClientX, ClientY, ClientZ, ClientFacing",
                [](uint32 stringID, uint64 key, uint32 time, uint32 session, uint32 map, float x, float y, float z, float facing)
                {
                    G::Game.Encryption.AddTextKeyInfo(stringID, { key, time, session, map, { x, y, z, facing } });
                },
                {
                    .Condition = "Key",
                    .SharedMutex = &G::Game.Encryption.Mutex(),
                    .PostHandler = [=]
                    {
                        G::Viewers::ForEach<UI::Viewers::StringListViewer>([](UI::Viewers::StringListViewer& viewer)
                        {
                            if (!viewer.FilterString.empty() && !viewer.FilterID)
                                viewer.UpdateSearch();
                            else if (viewer.Sort == UI::Viewers::StringListViewer::StringSort::Text || viewer.Sort == UI::Viewers::StringListViewer::StringSort::DecryptionTime)
                                viewer.UpdateSort();
                        });
                    },
                }
            ),
            LoadingOperation::Make("Assets",
                "AssetType, AssetID, Key",
                [](uint32 assetType, uint32 assetID, uint64 key)
                {
                    G::Game.Encryption.AddAssetKey((Encryption::AssetType)assetType, assetID, key);
                    if ((Encryption::AssetType)assetType == Encryption::AssetType::Voice)
                        G::Game.Voice.WipeCache(assetID);
                },
                {
                    .Condition = "Key",
                    .SharedMutex = &G::Game.Encryption.Mutex(),
                }
            ),
            LoadingOperation::Make("Conversations",
                "GenID, UID, FirstEncounteredTime, LastEncounteredTime",
                [](uint32 GenID, uint32 UID, uint32 FirstEncounteredTime, uint32 LastEncounteredTime)
                {
                    Content::conversations[GenID].UID = UID;
                },
                {
                    .SharedMutex = &Content::conversationsLock,
                    .PostHandler = updateConversationSearch,
                }
            ),
            LoadingOperation::Make("ConversationStates",
                "GenID, StateID, TextID, SpeakerNameTextID, SpeakerPortraitOverrideFileID, Priority, Flags, Voting, Timeout, CostAmount, CostType, Unk",
                [](uint32 GenID, uint32 StateID, uint32 TextID, uint32 SpeakerNameTextID, uint32 SpeakerPortraitOverrideFileID, uint32 Priority, uint32 Flags, uint32 Voting, uint32 Timeout, uint32 CostAmount, uint32 CostType, uint32 Unk)
                {
                    Content::conversations[GenID].States.emplace(StateID, TextID, SpeakerNameTextID, SpeakerPortraitOverrideFileID, Priority, Flags, Voting, Timeout, CostAmount, CostType, Unk);
                },
                {
                    .SharedMutex = &Content::conversationsLock,
                    .PostHandler = updateConversationSearch,
                }
            ),
            LoadingOperation::Make("ConversationStateTransitions",
                "GenID, StateID, StateTextID, TransitionID, TextID, CostAmount, CostType, CostKarma, Diplomacy, Unk, Personality, Icon, SkillDefDataID",
                [](uint32 GenID, uint32 StateID, uint32 StateTextID, uint32 TransitionID, uint32 TextID, uint32 CostAmount, uint32 CostType, uint32 CostKarma, uint32 Diplomacy, uint32 Unk, uint32 Personality, uint32 Icon, uint32 SkillDefDataID)
                {
                    for (auto& state : Content::conversations[GenID].States | std::views::filter([StateID, StateTextID](auto const& state) { return state.StateID == StateID && state.TextID == StateTextID; }))
                        state.Transitions.emplace(TransitionID, TextID, CostAmount, CostType, CostKarma, Diplomacy, Unk, Personality, Icon, SkillDefDataID);
                },
                {
                    .SharedMutex = &Content::conversationsLock,
                    .PostHandler = updateConversationSearch,
                }
            ),
            LoadingOperation::Make("ConversationStateTransitionTargets",
                "GenID, StateID, StateTextID, TransitionID, TransitionTextID, TargetStateID, Flags",
                [](uint32 GenID, uint32 StateID, uint32 StateTextID, uint32 TransitionID, uint32 TransitionTextID, uint32 TargetStateID, uint32 Flags)
                {
                    for (auto& state : Content::conversations[GenID].States | std::views::filter([StateID, StateTextID](auto const& state) { return state.StateID == StateID && state.TextID == StateTextID; }))
                        for (auto& transition : state.Transitions | std::views::filter([TransitionID, TransitionTextID](auto const& transition) { return transition.TransitionID == TransitionID && transition.TextID == TransitionTextID; }))
                            transition.Targets.emplace(TargetStateID, Flags);
                },
                {
                    .SharedMutex = &Content::conversationsLock,
                    .PostHandler = updateConversationSearch,
                }
            ),
            LoadingOperation::Make("AgentConversation",
                "ConversationGenID, ConversationStateID, ConversationStateTextID, ConversationStateTransitionID, ConversationStateTransitionTextID, Time, Session, Map, AgentX, AgentY, AgentZ, AgentFacing",
                [](uint32 ConversationGenID, uint32 ConversationStateID, uint32 ConversationStateTextID, uint32 ConversationStateTransitionID, uint32 ConversationStateTransitionTextID, uint64 Time, uint32 Session, uint32 Map, float AgentX, float AgentY, float AgentZ, float AgentFacing)
                {
                    auto& conversation = Content::conversations[ConversationGenID];
                    conversation.EncounteredTime = std::chrono::system_clock::time_point { std::chrono::milliseconds(Time) };
                    conversation.Session = Session;
                    conversation.Map = Map;
                    conversation.Position = { AgentX, AgentY, AgentZ, AgentFacing };

                    for (auto& state : conversation.States | std::views::filter([ConversationStateID, ConversationStateTextID](auto const& state) { return state.StateID == ConversationStateID && state.TextID == ConversationStateTextID; }))
                    {
                        state.EncounteredTime = conversation.EncounteredTime;
                        state.Session = conversation.Session;
                        state.Map = conversation.Map;
                        state.Position = conversation.Position;

                        if (ConversationStateTextID != -1)
                        {
                            for (auto& transition : state.Transitions | std::views::filter([ConversationStateTransitionID, ConversationStateTransitionTextID](auto const& transition) { return transition.TransitionID == ConversationStateTransitionID && transition.TextID == ConversationStateTransitionTextID; }))
                            {
                                transition.EncounteredTime = conversation.EncounteredTime;
                                transition.Session = conversation.Session;
                                transition.Map = conversation.Map;
                                transition.Position = conversation.Position;
                            }
                        }
                    }
                },
                {
                    .SharedMutex = &Content::conversationsLock,
                    .PostHandler = updateConversationSearch,
                }
            ),
            LoadingOperation::Make("Events",
                "Map, UID, TitleTextID, TitleParameterTextID1, TitleParameterTextID2, TitleParameterTextID3, TitleParameterTextID4, TitleParameterTextID5, TitleParameterTextID6, DescriptionTextID, FileIconID, FlagsClient, FlagsServer, Level, MetaTextTextID, AudioEffect, A, Time",
                [](uint32 Map, uint32 UID, uint32 TitleTextID, uint32 TitleParameterTextID1, uint32 TitleParameterTextID2, uint32 TitleParameterTextID3, uint32 TitleParameterTextID4, uint32 TitleParameterTextID5, uint32 TitleParameterTextID6, uint32 DescriptionTextID, uint32 FileIconID, Content::Event::State::ClientFlags FlagsClient, Content::Event::State::ServerFlags FlagsServer, uint32 Level, uint32 MetaTextTextID, GUID const& AudioEffect, uint32 A, uint64 Time)
                {
                    Content::events[{ Map, UID }].States.emplace(Map, UID, TitleTextID, std::array { TitleParameterTextID1, TitleParameterTextID2, TitleParameterTextID3, TitleParameterTextID4, TitleParameterTextID5, TitleParameterTextID6 }, DescriptionTextID, FileIconID, FlagsClient, FlagsServer, Level, MetaTextTextID, AudioEffect, A, Time).first->Time = Time;
                },
                {
                    .SharedMutex = &Content::eventsLock,
                    .PostHandler = updateEventFilter,
                }
            ),
            LoadingOperation::Make("Objectives",
                "Map, EventUID, EventObjectiveIndex, Type, Flags, TargetCount, TextID, AgentNameTextID, ProgressBarStyle, ExtraInt, ExtraInt2, ExtraGUID, ExtraGUID2, ExtraBlob, Time",
                [](uint32 Map, uint32 EventUID, uint32 EventObjectiveIndex, uint32 Type, uint32 Flags, uint32 TargetCount, uint32 TextID, uint32 AgentNameTextID, GUID const& ProgressBarStyle, uint32 ExtraInt, uint32 ExtraInt2, GUID const& ExtraGUID, GUID const& ExtraGUID2, std::vector<byte> const& ExtraBlob, uint64 Time)
                {
                    Content::events[{ Map, EventUID }].Objectives.emplace(Map, EventUID, EventObjectiveIndex, Type, Flags, TargetCount, TextID, AgentNameTextID, ProgressBarStyle, ExtraInt, ExtraInt2, ExtraGUID, ExtraGUID2, ExtraBlob, Time).first->Time = Time;
                },
                {
                    .SharedMutex = &Content::eventsLock,
                    .PostHandler = updateEventFilter,
                }
            ),
            LoadingOperation::Make("ObjectiveAgents",
                "ObjectiveMap, ObjectiveEventUID, ObjectiveEventObjectiveIndex, ObjectiveAgentIndex, ObjectiveAgentID, IFNULL(NULLIF(ObjectiveAgentNameTextID, 0), NameTextID), ObjectiveAgentX, ObjectiveAgentY, ObjectiveAgentZ, ObjectiveAgentFacing",
                [](uint32 ObjectiveMap, uint32 ObjectiveEventUID, uint32 ObjectiveEventObjectiveIndex, uint32 ObjectiveAgentIndex, uint32 ObjectiveAgentID, uint32 ObjectiveAgentNameTextID, float ObjectiveAgentX, float ObjectiveAgentY, float ObjectiveAgentZ, float ObjectiveAgentFacing)
                {
                    for (auto& objective : Content::events[{ ObjectiveMap, ObjectiveEventUID }].Objectives | std::views::filter([ObjectiveEventObjectiveIndex](auto const& objective) { return objective.EventObjectiveIndex == ObjectiveEventObjectiveIndex; }))
                    {
                        objective.Agents.resize(std::max<size_t>(objective.Agents.size(), ObjectiveAgentIndex + 1));
                        objective.Agents.at(ObjectiveAgentIndex) = { ObjectiveAgentID, ObjectiveAgentNameTextID };
                    }
                },
                {
                    .Joins = "LEFT JOIN Agents a ON ObjectiveAgents.Session=a.Session AND ObjectiveAgents.MapSession=a.MapSession AND ObjectiveAgents.ObjectiveAgentID=a.AgentID",
                    .SharedMutex = &Content::eventsLock,
                    .PostHandler = updateEventFilter,
                }
            ),
        };
        static bool exitRequested = false;
        static auto load = [=]
        {
            while (!exitRequested)
            {
                try
                {
                    for (auto&& operation : operations)
                        operation->Process(db);
                }
                catch (errors::busy const&) { }
                for (auto&& operation : operations)
                    operation->PostProcess();
                std::this_thread::sleep_for(1s);
            }
        };
        static std::thread thread(load);
        std::atexit([]
        {
            exitRequested = true;
            thread.join();
        });
    }
};

}

export namespace GW2Viewer::G { Data::External::Database Database; }
