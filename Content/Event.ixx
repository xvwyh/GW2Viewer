export module GW2Viewer.Content.Event;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import GW2Viewer.Content;
import GW2Viewer.Data.Game;
import GW2Viewer.Utils.Enum;
import std;

export namespace GW2Viewer::Content
{

struct EventID
{
    uint32 Map;
    uint32 UID;

    auto operator<=>(EventID const& other) const = default;
};
struct Event
{
    struct State
    {
        enum class ClientFlags : uint32
        {
            Active = 0x1,
            DungeonEvent = 0x2,
            Unk4 = 0x4, // related to IsDungeonEvent
            UIVisible = 0x8, // relate to UIVisible
            Hidden = 0x10, // related to dungeon events?
            GroupEvent = 0x20,
            MapWide = 0x40,
            MetaEvent = 0x80,
            Unk100 = 0x100,
            EVENT_MARKER_CONTEXT_REGISTERED = 0x200,
            AudioEffectAdded = 0x400,
            Unk800 = 0x800,
            Unk1000 = 0x1000, // something marker-related is 0.25 instead of 1.0
            IgnoreEventGroupState = 0x2000, // show regardless of whether the event group event is active

            AddedFromServerFlags = DungeonEvent | GroupEvent | MapWide | MetaEvent | Unk800 | Unk1000 | IgnoreEventGroupState,
            AddedByServerDynamically = Active | Hidden | Unk100,
            ClientsideFlags = Unk4 | UIVisible | EVENT_MARKER_CONTEXT_REGISTERED | AudioEffectAdded,
            ExcludedFlags = ClientsideFlags | Active,
        };
        enum class ServerFlags : uint32
        {
            DungeonEvent = 0x2,
            GroupEvent = 0x8,
            MapWide = 0x20,
            MetaEvent = 0x40,
            Unk800 = 0x800,
            Unk1000 = 0x4000, // something marker-related is 0.25 instead of 1.0
            IgnoreEventGroupState = 0x8000, // show regardless of whether the event group event is active
        };

        uint32 Map;
        uint32 UID;
        uint32 TitleTextID;
        std::array<uint32, 6> TitleParameterTextID;
        uint32 DescriptionTextID;
        uint32 FileIconID;
        ClientFlags FlagsClient;
        ServerFlags FlagsServer;
        uint32 Level;
        uint32 MetaTextTextID;
        GUID AudioEffect;
        uint32 A;

        mutable uint64 Time;

        bool IsDungeonEvent() const { return FlagsClient & ClientFlags::DungeonEvent & ClientFlags::DungeonEvent || FlagsServer & ServerFlags::DungeonEvent; }
        bool IsGroupEvent() const { return FlagsClient & ClientFlags::GroupEvent || FlagsServer & ServerFlags::GroupEvent; }
        bool IsMetaEvent() const { return FlagsClient & ClientFlags::MetaEvent || FlagsServer & ServerFlags::MetaEvent; }
        bool IsNormalEvent() const { return !IsDungeonEvent() && !IsGroupEvent() && !IsMetaEvent(); }

        auto GetIdentity() const { return std::tie(Map, UID, TitleTextID, TitleParameterTextID, DescriptionTextID, FileIconID, FlagsClient, FlagsServer, Level, MetaTextTextID, AudioEffect, A); }
        auto operator<=>(State const& other) const { return GetIdentity() <=> other.GetIdentity(); }
    };
    struct Objective
    {
        uint32 Map;
        uint32 EventUID;
        uint32 EventObjectiveIndex;
        uint32 Type;
        uint32 Flags = 0;
        uint32 TargetCount = 0;
        uint32 TextID;
        uint32 AgentNameTextID;
        GUID ProgressBarStyle;
        uint32 ExtraInt = 0;
        uint32 ExtraInt2 = 0;
        GUID ExtraGUID;
        GUID ExtraGUID2;
        std::vector<byte> ExtraBlob;

        mutable uint64 Time;

        mutable std::vector<std::tuple<uint32, uint32>> Agents;

        auto GetIdentity() const { return std::tie(Map, EventUID, EventObjectiveIndex, Type, Flags, TargetCount, TextID, AgentNameTextID, ProgressBarStyle, ExtraInt, ExtraInt2, ExtraGUID, ExtraGUID2, ExtraBlob); }
        auto operator<=>(Objective const& other) const { return GetIdentity() <=> other.GetIdentity(); }
    };

    std::set<State> States;
    std::set<Objective> Objectives;

    std::wstring Map() const
    {
        for (auto const& state : States)
            if (auto const object = G::Game.Content.GetByDataID(MapDef, state.Map))
                return object->GetDisplayName();
        for (auto const& objective : Objectives)
            if (auto const object = G::Game.Content.GetByDataID(MapDef, objective.Map))
                return object->GetDisplayName();
        return { };
    }
    std::string Type() const
    {
        std::set<std::string> types;
        for (auto const& state : States)
        {
            if (state.IsDungeonEvent())
                types.emplace("Dungeon");
            if (state.IsMetaEvent())
                types.emplace("Meta");
            if (state.IsGroupEvent())
                types.emplace("Group");
        }
        return std::format("{}", std::string(std::from_range, types | std::views::join_with(std::string_view(", "))));
    }
    std::wstring Title() const
    {
        for (auto const& state : States)
            if (auto string = G::Game.Text.Get(state.TitleTextID).first)
                return *string;
        return { };
    }
    auto EncounteredTime() const
    {
        std::chrono::system_clock::time_point result;
        for (auto const& state : States)
            result = std::max(result, decltype(result) { std::chrono::milliseconds(state.Time) });
        for (auto const& objective : Objectives)
            result = std::max(result, decltype(result) { std::chrono::milliseconds(objective.Time) });
        return result;
    }
};
std::map<EventID, Event> events;
std::shared_mutex eventsLock;

}
