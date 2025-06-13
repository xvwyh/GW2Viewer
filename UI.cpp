#include "UI.h"

#include "ArchiveManager.h"
#include "BankFileData.h"
#include "BankIndexData.h"
#include "Config.h"
#include "Content.h"
#include "Encryption.h"
#include "HexViewer.h"
#include "IconsFontAwesome6.h"
#include "MapLayoutViewer.h"
#include "PackContent.h"
#include "PackFile.h"
#include "PackFileLayout.h"
#include "PackFileLayoutTraversal.h"
#include "PackTextManifest.h"
#include "PackTextVariants.h"
#include "PackTextVoices.h"
#include "RC4.h"
#include "StringsFile.h"
#include "Symbols.h"
#include "Texture.h"
#include "Utils.h"

#include "dep/fmod/fmod.hpp"
#include "dep/imgui/imgui_impl_dx11.h"

#include <boost/algorithm/string.hpp>
#include <cpp-base64/base64.cpp>
#include <magic_enum_all.hpp>
#include <gsl/gsl>
#include <picosha2.h>
#include <scn/scn.h>
#include <sqlite_modern_cpp.h>

#include <bitset>
#include <execution>
#include <shared_mutex>
#include <stack>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

template<typename Rep, typename Period>
std::string format_duration(std::chrono::duration<Rep, Period> duration)
{
    using namespace std::chrono;

    constexpr std::tuple units { years(1), months(1), days(1), 1h, 1min, 1s, 1ms, 1us, 1ns };

    std::string result;

    auto suffix = overloaded
    {
        [](years const&) { return "y"; },
        [](months const&) { return "mo"; },
        [](minutes const&) { return "m"; },
        [](auto const& duration) { return std::format("{:%q}", duration); }
    };
    auto append = [&]<typename T>(T const& unit)
    {
        if constexpr (std::ratio_less_equal_v<Period, typename T::period>)
        {
            if (auto const val = duration_cast<T>(duration); val.count() > 0)
            {
                result += std::format("{}{} ", val.count(), suffix(unit));
                duration -= val;
            }
        }
    };

    std::apply([&](auto const&... unit) { (append(unit), ...); }, units);

    if (!result.empty())
        result.pop_back();
    return result;
}

template<typename Rep, typename Period>
std::string format_duration_colored(char const* format, std::chrono::duration<Rep, Period> duration)
{
    auto const color =
        duration < 10s ? "F00" :
        duration < 1min ? "F40" :
        duration < 10min ? "F80" :
        duration < 30min ? "FB0" :
        duration < 1h ? "FD0" :
        duration < 3h ? "FF0" :
        duration < 5h ? "FF8" :
        duration < 10h ? "FFC" :
        duration < 24h ? "FFF" :
        duration < 48h ? "C" :
        duration < 72h ? "8" : "4";
    return std::format("<c=#{}>{}</c>", color, std::vformat(format, std::make_format_args(format_duration(duration))));
}

#undef InterlockedIncrement
struct AsyncScheduler
{
    struct ProgressContext
    {
        bool Cancelled = false;
        uint32 Current = 0;
        uint32 Total = 1;
        std::mutex Lock;
        ProgressContext(bool cancelled = false) : Cancelled(cancelled) { }
        ProgressContext(ProgressContext const& source) : Cancelled(source.Cancelled), Current(source.Current), Total(source.Total) { }
        operator bool() const { return !Cancelled; }

        void Cancel() { Cancelled = true; }
        void Finish() { Cancel(); }
        void Clear() { Current = 0; }
        void Increment(uint32 amount = 1) { Current += amount; }
        void InterlockedIncrement(uint32 amount = 1) { std::scoped_lock _(Lock); Current += amount; }
        void SetTotal(uint32 total) { Total = total; }
        void SetIndeterminate() { SetTotal(0); }
        bool IsIndeterminate() const { return !Total; }
        float Progress() const { return (float)Current / Total; }
        float ProgressPercent() const { return 100.0f * Progress(); }
    };
    using Context = std::shared_ptr<ProgressContext>;

    AsyncScheduler(bool allowParallelTasks = false) : m_allowParallelTasks(allowParallelTasks) { }

    ProgressContext Current() const
    {
        std::scoped_lock _(m_lock);
        return m_current && !m_current->Cancelled ? *m_current : ProgressContext { true };
    }
    void Run(auto&& task)
    {
        std::scoped_lock _(m_lock);
        if (auto const previous = std::exchange(m_current, std::make_shared<Context::element_type>()))
            previous->Cancel();

        if (m_allowParallelTasks)
            m_runningTasks.remove_if([](std::future<void> const& future) { return future.wait_for(0s) == std::future_status::ready; });
        else
            m_runningTasks.clear(); // This joins the ongoing task thread and waits for its completion, which should be fast thanks to many cancellation checks

        m_runningTasks.emplace_back(std::async(std::launch::async, std::move(task), m_current));
    }

private:
    bool m_allowParallelTasks = false;
    std::list<std::future<void>> m_runningTasks;
    Context m_current = nullptr;
    mutable std::mutex m_lock;
};
using AsyncContext = AsyncScheduler::Context;
#define CHECK_ASYNC do { if (!context || context->Cancelled) { context.reset(); return; } } while (false)
#define CHECK_SHARED_ASYNC do { if (!context || context->Cancelled) { context->Cancel(); return; } } while (false)

STATIC(g_ui);

std::vector<ArchiveFile> filteredFiles;
std::span<ArchiveFile> searchedFiles;
std::vector<uint32> filteredStrings;
std::shared_mutex stringsLock;
std::shared_mutex contentLock;

struct Conversation
{
    enum Completeness
    {
        COMPLETENESS_TRANSITION_TARGET_MISSING,
        COMPLETENESS_PRESUMABLY_MISSING,
        COMPLETENESS_TRANSITION_EXIT_TARGET_MISSING,
        COMPLETENESS_COMPLETE,
    };

    struct State
    {
        struct Transition
        {
            struct Target
            {
                uint32 TargetStateID;
                uint32 Flags;

                auto operator<=>(Target const&) const = default;
            };

            uint32 TransitionID;
            uint32 TextID;
            uint32 CostAmount;
            uint32 CostType;
            uint32 CostKarma;
            uint32 Diplomacy;
            uint32 Unk;
            uint32 Personality;
            uint32 Icon;
            uint32 SkillDefDataID;

            mutable std::set<Target> Targets;

            Completeness GetCompleteness() const
            {
                if (Targets.empty())
                    return Icon == 14 ? COMPLETENESS_TRANSITION_EXIT_TARGET_MISSING : COMPLETENESS_TRANSITION_TARGET_MISSING;
                return COMPLETENESS_COMPLETE;
            }

            auto GetIdentity() const { return std::tie(TransitionID, TextID, CostAmount, CostType, CostKarma, Diplomacy, Unk, Personality, Icon, SkillDefDataID); }
            auto operator<=>(Transition const& other) const { return GetIdentity() <=> other.GetIdentity(); }

            mutable std::chrono::system_clock::time_point EncounteredTime;
            mutable uint32 Session { };
            mutable uint32 Map { };
            mutable ImVec4 Position { };
        };

        uint32 StateID;
        uint32 TextID;
        uint32 SpeakerNameTextID;
        uint32 SpeakerPortraitOverrideFileID;
        uint32 Priority;
        uint32 Flags;
        uint32 Voting;
        uint32 Timeout;
        uint32 CostAmount;
        uint32 CostType;
        uint32 Unk;

        mutable std::set<Transition> Transitions;
        mutable std::set<Transition> ScriptedTransitions;

        bool IsVoting() const { return Flags & 0x20; }
        bool IsStart() const { return Flags & 0x10; }
        bool IsExit() const { return Flags & 0x3; }

        Completeness GetCompleteness() const
        {
            if (Transitions.empty())
                return COMPLETENESS_COMPLETE;

            auto const completeness = std::ranges::min(Transitions | std::views::transform(&Transition::GetCompleteness));
            std::vector<uint32> unique;
            unique.reserve(Transitions.size());
            std::ranges::unique_copy(Transitions | std::views::transform(&Transition::TransitionID), std::back_inserter(unique));
            if (unique.size() != std::ranges::max(unique) + 1)
                return std::min(completeness, COMPLETENESS_PRESUMABLY_MISSING);
            return completeness;
        }
        bool IsScriptedStateState() const { return /*!StateID &&*/ !TextID && !SpeakerNameTextID && Transitions.empty(); }

        auto GetIdentity() const { return std::tie(StateID, TextID, SpeakerNameTextID, SpeakerPortraitOverrideFileID, Priority, Flags, Voting, Timeout, CostAmount, CostType, Unk); }
        auto operator<=>(State const& other) const { return GetIdentity() <=> other.GetIdentity(); }

        mutable std::chrono::system_clock::time_point EncounteredTime;
        mutable uint32 Session { };
        mutable uint32 Map { };
        mutable ImVec4 Position { };
    };

    uint32 UID;

    mutable std::set<State> States;

    Completeness GetCompleteness() const
    {
        if (States.empty())
            return COMPLETENESS_COMPLETE;

        auto const completeness = std::ranges::min(States | std::views::transform(&State::GetCompleteness));
        std::vector<uint32> unique;
        unique.reserve(States.size());
        std::ranges::unique_copy(States | std::views::transform(&State::StateID), std::back_inserter(unique));
        if (unique.size() != std::ranges::max(unique) + 1)
            return std::min(completeness, COMPLETENESS_PRESUMABLY_MISSING);
        return completeness;
    }
    bool HasMultipleSpeakers() const
    {
        std::optional<std::wstring> first;
        return std::ranges::any_of(States | std::views::transform(&State::SpeakerNameTextID) | std::views::filter(std::identity()), [&](uint32 speakerNameTextID)
        {
            if (auto [string, status] = GetString(speakerNameTextID); string)
            {
                if (first)
                    return *first != *string;
                first.emplace(*string);
            }
            return false;
        });
    }
    bool HasScriptedStart() const { return std::ranges::any_of(States, &State::IsScriptedStateState); }

    std::string StartingSpeakerName() const
    {
        std::string result;
        for (std::set<uint32> uniqueSpeakers; auto const speakerNameTextID : States | std::views::transform(&State::SpeakerNameTextID) | std::views::filter(std::identity()))
            if (uniqueSpeakers.emplace(speakerNameTextID).second)
                if (auto string = GetString(speakerNameTextID).first)
                    result = std::format("{}{}{}", result, result.empty() ? "" : ", ", to_utf8(*string));
        return result;
    }
    std::string StartingStateText() const
    {
        std::string result;
        for (auto const& state : States | std::views::filter([](auto const& state) { return state.TextID; }))
            if (result.empty() || state.IsStart())
                if (auto string = GetString(state.TextID).first)
                    if (result = to_utf8(*string), state.IsStart())
                        break;

        replace_all(result, "\r", R"(<c=#F00>\r</c>)");
        replace_all(result, "\n", R"(<c=#F00>\n</c>)");
        return result;
    }
    std::chrono::system_clock::time_point EncounteredTime;
    uint32 Session { };
    uint32 Map { };
    ImVec4 Position { };
};
std::map<uint32, Conversation> conversations;
std::shared_mutex conversationsLock;
std::vector<uint32> filteredConversations;

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
            if (auto const object = GetContentObjectByDataID(Content::MapDef, state.Map))
                return object->GetDisplayName();
        for (auto const& objective : Objectives)
            if (auto const object = GetContentObjectByDataID(Content::MapDef, objective.Map))
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
            if (auto string = GetString(state.TitleTextID).first)
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
std::vector<EventID> filteredEvents;

bool DrawTexture(uint32 textureFileID, DrawTextureOptions const& options)
{
    if (auto const texture = GetTexture(textureFileID); !texture || texture->TextureLoadingState == TextureEntry::TextureLoadingStates::NotLoaded)
        LoadTexture(textureFileID, { .DataSource = options.Data });
    else if (texture && texture->Texture && texture->Texture->Handle)
    {
        ImVec2 const fullSize { (float)texture->Texture->Width, (float)texture->Texture->Height };
        ImVec2 offset { };
        ImVec2 size = options.Size;
        if (!size.x && !size.y)
            size = fullSize;
        else if (!size.x)
            size.x = options.PreserveAspectRatio ? size.y * (fullSize.x / fullSize.y) : size.y;
        else if (!size.y)
            size.y = options.PreserveAspectRatio ? size.x * (fullSize.y / fullSize.x) : size.x;
        else if (options.PreserveAspectRatio)
            offset = (options.Size - (size = fullSize * std::min(size.x / fullSize.x, size.y / fullSize.y))) * 0.5f;
        auto const pos = I::GetCursorScreenPos();
        ImRect const bb { pos, pos + size + offset * 2 };
        if (options.AdvanceCursor)
            I::ItemSize(bb);
        I::ItemAdd(bb, 0);
        if (auto& draw = *I::GetWindowDrawList(); options.UV2 && options.Color.w > 0)
        {
            const bool push_texture_id = texture->Texture->Handle != draw._CmdHeader.TextureId;
            if (push_texture_id)
                draw.PushTextureID(texture->Texture->Handle);

            draw.PrimReserve(6, 4);
            draw.PrimRectUV(bb.Min + offset, bb.Max - offset, options.UV.Min, options.UV.Max, I::ColorConvertFloat4ToU32(options.Color));
            draw._VtxWritePtr[-4].uv2 = options.UV2->GetTL();
            draw._VtxWritePtr[-3].uv2 = options.UV2->GetTR();
            draw._VtxWritePtr[-2].uv2 = options.UV2->GetBR();
            draw._VtxWritePtr[-1].uv2 = options.UV2->GetBL();

            if (push_texture_id)
                draw.PopTextureID();
        }
        else
            draw.AddImage(texture->Texture->Handle, bb.Min + offset, bb.Max - offset, options.UV.Min, options.UV.Max, I::ColorConvertFloat4ToU32(options.Color));
        if (options.FullPreviewOnHover && size != fullSize)
            if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
                I::Image(texture->Texture->Handle, fullSize);
        return true;
    }
    if (options.ReserveSpace && (options.Size.x || options.Size.y))
    {
        auto const pos = I::GetCursorScreenPos();
        ImRect const bb { pos, pos + options.Size };
        if (options.AdvanceCursor)
            I::ItemSize(bb);
        I::ItemAdd(bb, 0);
    }
    return false;
}
void DrawContentButton(ContentObject* content, void const* id, DrawContentButtonOptions const& options)
{
    scoped::WithID(id);

    DrawContentButtonOptions::CondenseContext localCondense;
    auto& condense = options.SharedCondenseContext ? *options.SharedCondenseContext : localCondense;

    std::string textPreIcon, textPostIcon;
    ImVec2 sizePreIcon, sizePostIcon, size;
    auto const icon = content ? content->GetIcon() : 0;
    auto const iconSize = icon ? ImVec2(I::GetFrameHeight(), I::GetFrameHeight()) : ImVec2();
    auto const padding = I::GetStyle().FramePadding;
    do
    {
        textPreIcon = std::vformat(condense.TypeName ? "{} " : "{} <c=#4>{}</c> ", std::make_format_args(options.Icon, content ? to_utf8(content->Type->GetDisplayName()) : options.MissingTypeName));
        textPostIcon = std::format("{}", content ? to_utf8(condense.FullName ? content->GetDisplayName() : content->GetFullDisplayName()) : options.MissingContentName);
        sizePreIcon = I::CalcTextSize(textPreIcon.c_str(), textPreIcon.c_str() + textPreIcon.size());
        sizePostIcon = I::CalcTextSize(textPostIcon.c_str(), textPostIcon.c_str() + textPostIcon.size());
        size = padding * 2;
        size.x += sizePreIcon.x + iconSize.x + sizePostIcon.x;
        size.y += std::max(sizePreIcon.y, sizePostIcon.y);
    }
    while (size.x > I::GetContentRegionAvail().x && condense.Condense());

    auto const pos = I::GetCursorScreenPos();
    I::Button("", size);
    if (content)
        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
            g_ui.OpenContent(*content, button & ImGuiButtonFlags_MouseButtonMiddle);

    ImRect bb(pos, pos + size);
    I::RenderTextClipped(bb.Min + padding, bb.Max - padding, textPreIcon.c_str(), textPreIcon.c_str() + textPreIcon.size(), &sizePreIcon, { }, &bb);
    bb.Min.x += sizePreIcon.x;
    if (icon)
        if (scoped::WithCursorScreenPos(bb.Min + ImVec2(padding.x, 0)))
            if (DrawTexture(icon, { .Size = iconSize, .AdvanceCursor = false }))
                bb.Min.x += iconSize.x;
    I::RenderTextClipped(bb.Min + padding, bb.Max - padding, textPostIcon.c_str(), textPostIcon.c_str() + textPostIcon.size(), &sizePostIcon, { }, &bb);
}
void DrawVoiceButton(uint32 voiceID, DrawVoiceButtonOptions const& options)
{
    scoped::WithID(voiceID);
    auto const status = GetVoiceStatus(voiceID, g_config.Language, GetDecryptionKey(EncryptedAssetType::Voice, voiceID));
    static constexpr char const* VARIANT_NAMES[] { "Asura Male", "Asura Female", "Charr Male", "Charr Female", "Human Male", "Human Female", "Norn Male", "Norn Female", "Sylvari Male", "Sylvari Female" };
    std::string const text = std::vformat(options.MenuItem ? "<c=#{}>{} {} ({})</c>##Play" : "<c=#{}>{} {}</c>##Play", std::make_format_args(
        GetEncryptionStatusColor(status),
        status == EncryptionStatus::Missing || status == EncryptionStatus::Encrypted ? GetEncryptionStatusText(status) : ICON_FA_PLAY,
        voiceID,
        VARIANT_NAMES[options.VariantIndex]));
    if ([&]
    {
        if (options.MenuItem)
            return I::MenuItem(text.c_str());
        if (options.Selectable)
            return I::Selectable(text.c_str());
        return I::Button(text.c_str());

    }())
        g_ui.PlayVoice(voiceID);
}
void DrawTextVoiceButton(uint32 textID, DrawVoiceButtonOptions const& options)
{
    scoped::WithID(textID);
    if (auto const variantItr = g_textVariants.find(textID); variantItr != g_textVariants.end())
    {
        auto const status = std::ranges::fold_left(variantItr->second, EncryptionStatus::Missing, [](EncryptionStatus value, uint32 variant)
        {
            switch (GetVoiceStatus(variant, g_config.Language, GetDecryptionKey(EncryptedAssetType::Voice, variant)))
            {
                using enum EncryptionStatus;
                case Missing: break;
                case Encrypted: return Encrypted;
                case Decrypted: if (value != Encrypted) return Decrypted; break;
                case Unencrypted: if (value != Encrypted && value != Decrypted) return Unencrypted; break;
            }
            return value;
        });

        std::string const text = std::format("<c=#{}>{} Variant " ICON_FA_CHEVRON_DOWN "</c>##PlayVariant",
            GetEncryptionStatusColor(status),
            status == EncryptionStatus::Missing || status == EncryptionStatus::Encrypted ? GetEncryptionStatusText(status) : ICON_FA_PLAY);
        if (options.MenuItem)
            I::MenuItem(text.c_str());
        else if (options.Selectable)
            I::Selectable(text.c_str());
        else
            I::Button(text.c_str());

        if (scoped::PopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft))
        {
            for (auto const& [index, variant] : variantItr->second | std::views::enumerate)
            {
                DrawVoiceButtonOptions menuItemOptions = options;
                menuItemOptions.MenuItem = true;
                menuItemOptions.VariantIndex = index;
                DrawVoiceButton(variant, menuItemOptions);
            }
        }
    }
    else if (auto const voiceItr = g_textVoices.find(textID); voiceItr != g_textVoices.end())
        DrawVoiceButton(voiceItr->second, options);
}
void DrawCopyButton(char const* name, std::string const& data, bool condition)
{
    if (scoped::Disabled(!condition))
    if (I::Button(std::vformat(condition ? ICON_FA_COPY " <c=#8>{}:</c> {}" : ICON_FA_COPY " <c=#8>{}</c>", std::make_format_args(name, I::StripMarkup(data))).c_str()))
        I::SetClipboardText(I::StripMarkup(data).c_str());
}

struct DBLoadingOperation
{
    struct Options
    {
        std::string Joins = "";
        std::string Condition = "1";
        std::shared_mutex* SharedMutex = nullptr;
        std::function<void()> PostHandler = nullptr;
    };

    virtual ~DBLoadingOperation() = default;
    virtual void Process(sqlite::database& db) = 0;
    virtual void PostProcess() = 0;

    static auto Make(std::string_view table, std::string_view columns, auto&& handler, Options&& options = { });
};
template<typename... Args>
struct DBLoadingOperationT : DBLoadingOperation
{
    std::string Table;
    std::string Columns;
    std::function<void(Args...)> Handler;
    Options Options;
    std::string Query = std::format("SELECT {}._rowid_, {} FROM {} {} WHERE {}._rowid_ > ? AND ({})", Table, Columns, Table, Options.Joins, Table, Options.Condition);
    sqlite_int64 MaxRowID = -1;
    sqlite_int64 LastMaxRowID = -1;

    DBLoadingOperationT(std::string_view table, std::string_view columns, std::function<void(Args...)>&& handler, DBLoadingOperation::Options&& options) : Table(table), Columns(columns), Handler(std::move(handler)), Options(std::move(options)) { }

    template<typename T> struct CoerceArgumentType { using Type = T; };
    template<> struct CoerceArgumentType<uint64> { using Type = sqlite_uint64; };
    template<> struct CoerceArgumentType<int64> { using Type = sqlite_int64; };

    void Process(sqlite::database& db) override
    {
        if (Options.SharedMutex)
            Options.SharedMutex->lock();

        auto _ = gsl::finally([this]
        {
            if (Options.SharedMutex)
                Options.SharedMutex->unlock();
        });

        db << Query << MaxRowID >> [this](sqlite_int64 rowID, typename CoerceArgumentType<Args>::Type... args)
        {
            Handler(args...);
            MaxRowID = std::max(MaxRowID, rowID);
        };

    }
    void PostProcess() override
    {
        if (LastMaxRowID != MaxRowID)
        {
            LastMaxRowID = MaxRowID;
            if (Options.PostHandler)
                g_ui.Defer(std::function(Options.PostHandler));
        }
    }
};
auto DBLoadingOperation::Make(std::string_view table, std::string_view columns, auto&& handler, Options&& options)
{
    return std::unique_ptr<DBLoadingOperation>(new DBLoadingOperationT(table, columns, std::function(handler), std::move(options)));
}

void UI::Create()
{
    ImGuiIO& io = I::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    //io.ConfigWindowsMoveFromTitleBarOnly = true;

    io.Fonts->AddFontDefault();
    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphMinAdvanceX = 10.0f;
    ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    builder.AddRanges(std::array<ImWchar, 3> { 0x2000, 0x2100, 0 }.data());
    builder.BuildRanges(&ranges);
    auto loadFont = [&](const char* filename, float size)
    {
        static constexpr ImWchar faRanges[] { ICON_MIN_FA, ICON_MAX_FA, 0 };
        auto font = io.Fonts->AddFontFromFileTTF(std::format(R"(Resources\Fonts\{})", filename).c_str(), size, nullptr, ranges.Data);
        io.Fonts->AddFontFromFileTTF(R"(Resources\Fonts\)" FONT_ICON_FILE_NAME_FAS, 10.0f, &config, faRanges);
        return font;
    };
    Fonts.Default = loadFont("Roboto-Regular.ttf", 15.0f);
    Fonts.GameText = loadFont("trebuc.ttf", 15.5f);
    Fonts.GameTextItalic = loadFont("trebucit.ttf", 15.5f);
    Fonts.GameHeading = loadFont("menomonia.ttf", 18.0f);
    Fonts.GameHeadingItalic = loadFont("menomonia-italic.ttf", 18.0f);
    io.Fonts->Build();
    io.FontDefault = Fonts.Default;

    ImVec4* colors = I::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    //colors[ImGuiCol_DockingPreview] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    //colors[ImGuiCol_DockingEmptyBg] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

    colors[ImGuiCol_PlotHistogram] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

    ImGuiStyle& style = I::GetStyle();
    style.WindowPadding = ImVec2(8.00f, 8.00f);
    style.FramePadding = ImVec2(5.00f, 2.00f);
    style.CellPadding = ImVec2(6.00f, 6.00f);
    style.ItemSpacing = ImVec2(6.00f, 6.00f);
    style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
    style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
    style.IndentSpacing = 25;
    style.ScrollbarSize = 15;
    style.GrabMinSize = 10;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;
    style.TabBorderSize = 1;
    style.WindowRounding = 7;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 9;
    style.GrabRounding = 3;
    style.LogSliderDeadzone = 4;
    style.TabRounding = 4;
}

template<typename Index, typename ComplexIndex>
auto defaultComplexSortComparison(Index const a, Index const b, ComplexIndex const& aTransformed, ComplexIndex const& bTransformed) -> bool
{
    #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
    COMPARE(aTransformed, bTransformed);
    COMPARE(a, b);
    return false;
    #undef COMPARE
};

void UI::DrawFrame()
{
    auto const now = std::chrono::high_resolution_clock::now();
    static auto prev = now;
    m_deltaTime = std::max(0.001f, std::chrono::duration_cast<std::chrono::microseconds>(now - prev).count() / 1000000.0f);
    prev = now;

    while (!m_deferred.empty())
    {
        m_deferred.front()();
        m_deferred.pop_front();
    }

    struct CompareResult
    {
        CompareResult(std::strong_ordering result) : m_result(result) { }
        operator bool() const { return m_result != std::strong_ordering::equal; }
        operator std::strong_ordering() const { return m_result; }
    private:
        std::strong_ordering m_result;
    };
    static constexpr auto compare = [](auto&& a, auto&& b) -> CompareResult { return a <=> b; };

    static constexpr auto complexSort = []<typename Range, typename Index = typename Range::value_type, typename Transform, typename ComplexIndex = decltype(Transform{}(Index{})), typename Comparison = bool(Index, Index, ComplexIndex const&, ComplexIndex const&)>(Range& data, bool invert, Transform const& transform, Comparison const& function = defaultComplexSortComparison)
    {
        std::vector sortable { std::from_range, data | std::views::transform([&transform](auto const& id) { return std::pair { id, transform(id) }; }) };
        std::ranges::sort(sortable, [invert, &function](auto const& a, auto const& b) -> bool { return function(a.first, b.first, a.second, b.second) ^ invert; });
        data.assign_range(sortable | std::views::keys);
    };
    static constexpr auto success = [](auto const& result) { return result && result.empty(); };
    static constexpr auto successNumber = [](auto const& result)
    {
        if (!result)
            return false;
        if (result.empty())
            return true;
        if (auto rest = std::string_view(result.subrange());
            rest == "u" || rest == "U" ||
            rest == "ul" || rest == "UL" ||
            rest == "ull" || rest == "ULL" ||
            rest == "l" || rest == "L" ||
            rest == "ll" || rest == "LL" ||
            rest == "i64" || rest == "I64" ||
            rest == "ui64" || rest == "UI64" ||
            rest == "h")
            return true;
        return false;
    };

    auto updateFileSearch = [this]
    {
        if (m_fileFilterID >= 0x10000FF)
        {
            pf::FileReference ref;
            *(uint64*)&ref = m_fileFilterID;
            m_fileFilterID = ref.GetFileID();
        }
        if (m_fileFilterID)
        {
            auto const to = (uint32)std::max(0, (int32)m_fileFilterID + (int32)m_fileFilterRange);
            auto const from = (uint32)std::max(0, (int32)m_fileFilterID - (int32)m_fileFilterRange);
            searchedFiles = { std::ranges::lower_bound(filteredFiles, from, { }, &ArchiveFile::ID), std::ranges::upper_bound(filteredFiles, to, { }, &ArchiveFile::ID) };
            return;
        }
        searchedFiles = filteredFiles;
    };
    auto updateFileFilter = [this, updateFileSearch]
    {
        auto filter = [this](ArchiveFile const& file) { return true; }; // TODO
        filteredFiles.assign_range(g_archives.GetFiles() | std::views::filter(filter));
        updateFileSearch();
    };

    static AsyncScheduler asyncStringFilter { true };
    static auto sortStrings = [](AsyncContext context, std::vector<uint32>& data, StringSort sort, bool invert)
    {
        #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
        switch (sort)
        {
            using enum StringSort;
            case ID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a < b ^ invert; });
                break;
            case Text:
                complexSort(data, invert, [](uint32 id)
                {
                    auto [string, status] = GetString(id);
                    return string ? *string : L"";
                });
                break;
            case DecryptionTime:
                complexSort(data, invert, [](uint32 id) { return GetDecryptionKeyInfo(id); }, [](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    if (aInfo && bInfo)
                    {
                        COMPARE(aInfo->Time, bInfo->Time);
                        COMPARE(aInfo->Index, bInfo->Index);
                    }
                    else
                        COMPARE((bool)aInfo, (bool)bInfo);
                    COMPARE(a, b);
                    return false;
                });
                break;
            case Voice:
                complexSort(data, invert, [](uint32 id) -> uint32
                {
                    if (auto const variantItr = g_textVariants.find(id); variantItr != g_textVariants.end())
                        return variantItr->second.front();
                    if (auto const voiceItr = g_textVoices.find(id); voiceItr != g_textVoices.end())
                        return voiceItr->second;
                    return { };
                });
                break;
            default: std::terminate();
        }
        #undef COMPARE
    };
    static auto setStringsResult = [](AsyncContext context, std::vector<uint32>&& data)
    {
        if (context->Cancelled) return;
        std::unique_lock _(stringsLock);
        filteredStrings = std::move(data);
        context->Finish();
    };
    auto updateStringSort = [this]
    {
        asyncStringFilter.Run([sort = m_stringSort, invert = m_stringSortInvert](AsyncContext context)
        {
            context->SetIndeterminate();
            std::vector<uint32> data;
            {
                std::shared_lock _(stringsLock);
                data = filteredStrings;
            }
            CHECK_ASYNC;
            sortStrings(context, data, sort, invert);
            setStringsResult(context, std::move(data));
        });
    };
    auto updateStringSearch = [this]
    {
        bool textSearch = false;
        m_stringFilterID.reset();
        if (m_stringFilterString.empty())
            ;
        else if (uint32 id, range; success(scn::scan(m_stringFilterString, "{}+{}", id, range)))
            m_stringFilterID.emplace(id - range, id + range);
        else if (success(scn::scan(m_stringFilterString, "{}-{}", id, range)))
            m_stringFilterID.emplace(id, range);
        else if (successNumber(scn::scan_default(m_stringFilterString, id)))
            m_stringFilterID.emplace(id - m_stringFilterRange, id + m_stringFilterRange);
        else if (successNumber(scn::scan(m_stringFilterString, "0x{:x}", id)))
            m_stringFilterID.emplace(id - m_stringFilterRange, id + m_stringFilterRange);
        else
            textSearch = true;

        asyncStringFilter.Run([textSearch, filter = m_stringFilterID, string = m_stringFilterString, filters = m_stringFilters, sort = m_stringSort, invert = m_stringSortInvert](AsyncContext context) mutable
        {
            context->SetIndeterminate();
            auto limits = filter.value_or(std::pair { std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() });
            limits.first = std::max(0, limits.first);
            limits.second = std::min((int32)g_maxStringID - 1, limits.second);
            std::vector<uint32> data;
            data.resize(limits.second - limits.first + 1);
            std::ranges::iota(data, limits.first);
            CHECK_ASYNC;
            if (!(filters.Unencrypted && filters.Encrypted && filters.Decrypted && filters.Empty))
            {
                std::erase_if(data, [&filters](uint32 id)
                {
                    auto [string, status] = GetString(id);
                    if (!filters.Empty && string && (string->empty() || *string == L"[null]"))
                        return true;
                    switch (status)
                    {
                        using enum EncryptionStatus;
                        case Unencrypted: return !filters.Unencrypted;
                        case Encrypted: return !filters.Encrypted;
                        case Decrypted: return !filters.Decrypted;
                        case Missing: default: std::terminate();
                    }
                });
            }
            CHECK_ASYNC;
            if (textSearch)
            {
                static std::mutex parallelLock;
                std::unique_lock _(parallelLock);
                static std::unordered_map<std::thread::id, std::vector<uint32>> parallelResults;
                static std::mutex lock;
                std::ranges::for_each(parallelResults | std::views::values, &std::vector<uint32>::clear);
                std::wstring const query(std::from_range, from_utf8(string) | std::views::transform(toupper));
                std::for_each(std::execution::par_unseq, data.begin(), data.end(), [&query](uint32 stringID)
                {
                    thread_local auto& results = []() -> auto&
                    {
                        std::scoped_lock _(lock);
                        auto& container = parallelResults[std::this_thread::get_id()];
                        container.reserve(10000);
                        return container;
                    }();
                    if (auto const& [string, status] = GetNormalizedString(stringID); string && !string->empty() && string->contains(query))
                        results.emplace_back(stringID);
                });
                CHECK_ASYNC;
                data.assign_range(std::views::join(parallelResults | std::views::values));
            }
            CHECK_ASYNC;
            sortStrings(context, data, sort, invert);
            setStringsResult(context, std::move(data));
        });
    };
    auto updateStringFilter = updateStringSearch;

    static AsyncScheduler asyncContentFilter { true };
    static ContentFilter contentFilter;
    auto sortContent = [this](std::vector<uint32>& data, ContentSort sort, bool invert)
    {
        #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
        switch (sort)
        {
            using enum ContentSort;
            case GUID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a < b ^ invert; });
                break;
            case UID:
                complexSort(data, invert, [](uint32 id) { return g_contentObjects.at(id); }, [invert](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    COMPARE((invert ? bInfo : aInfo)->Type->Index, (invert ? aInfo : bInfo)->Type->Index);
                    COMPARE(a, b);
                    return false;
                });
                break;
            case DataID:
                complexSort(data, invert, [](uint32 id) { return g_contentObjects.at(id); }, [invert](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    COMPARE((invert ? bInfo : aInfo)->Type->Index, (invert ? aInfo : bInfo)->Type->Index);
                    if (auto const* aID = aInfo->GetDataID())
                        if (auto const* bID = bInfo->GetDataID())
                            COMPARE(*aID, *bID);
                    COMPARE(a, b);
                    return false;
                });
                break;
            case Type:
                complexSort(data, invert, [](uint32 id) { return g_contentObjects.at(id); }, [this, invert](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    COMPARE(aInfo->Type->Index, bInfo->Type->Index);
                    COMPARE((invert ? bInfo : aInfo)->GetDisplayName(m_showOriginalNames, true), (invert ? aInfo : bInfo)->GetDisplayName(m_showOriginalNames, true));
                    //if (_wcsicmp(aInfo->GetDisplayName(m_showOriginalNames, true).c_str(), bInfo->GetDisplayName(m_showOriginalNames, true).c_str()) < 0) return true;
                    COMPARE(a, b);
                    return false;
                });
                break;
            case Name:
                complexSort(data, invert, [](uint32 id) { return g_contentObjects.at(id); }, [this](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    COMPARE(aInfo->GetDisplayName(m_showOriginalNames, true), bInfo->GetDisplayName(m_showOriginalNames, true));
                    //if (_wcsicmp(aInfo->GetDisplayName(m_showOriginalNames, true).c_str(), bInfo->GetDisplayName(m_showOriginalNames, true).c_str()) < 0) return true;
                    COMPARE(a, b);
                    return false;
                });
                break;
            default: std::terminate();
        }
        #undef COMPARE
    };
    auto getSortedContentObjects = [this, sortContent, timeout = now + 10ms](bool isNamespace, uint32 index, std::list<std::unique_ptr<ContentObject>> const& entries)
    {
        struct Cache
        {
            ContentSort Sort;
            bool Invert;
            std::vector<uint32> Objects;
        };
        static std::unordered_map<uint32, Cache> namespaces, rootObjects;
        if (m_contentSortClearCache)
        {
            namespaces.clear();
            rootObjects.clear();
            m_contentSortClearCache = false;
        }
        auto& cache = (isNamespace ? namespaces : rootObjects)[index];
        bool reset = false;
        if (cache.Objects.size() != entries.size())
        {
            cache.Objects.assign_range(entries | std::views::transform([](auto const& ptr) { return ptr->Index; }));
            reset = true;
        }
        if ((reset || cache.Sort != m_contentSort || cache.Invert != m_contentSortInvert) && std::chrono::high_resolution_clock::now() < timeout)
            sortContent(cache.Objects, (cache.Sort = m_contentSort), (cache.Invert = m_contentSortInvert));
        return cache.Objects | std::views::transform([](uint32 index) { return g_contentObjects.at(index); });
    };
    auto updateContentSort = [] { };
    auto updateContentFilter = [this](bool delayed = false)
    {
        m_contentFilterName.clear();
        m_contentFilterGUID.reset();
        m_contentFilterUID = { -1, -1 };
        m_contentFilterDataID = { -1, -1 };
        int32 range;
        if (m_contentFilterString.empty())
            ;
        else if (std::string name; success(scn::scan(m_contentFilterString, R"("{:[^"]}")", name)))
            m_contentFilterName = name;
        else if (GUID guid; success(scn::scan_default(m_contentFilterString, guid)) || success(scn::scan(m_contentFilterString, "guid:{}", guid)))
            m_contentFilterGUID = guid;
        else if (uint32 dataID; success(scn::scan(m_contentFilterString, "{}+{}", dataID, range)) || success(scn::scan(m_contentFilterString, "id:{}+{}", dataID, range)) || success(scn::scan(m_contentFilterString, "dataid:{}+{}", dataID, range)))
            m_contentFilterDataID = { (uint32)std::max(0, (int32)dataID - range), dataID + range };
        else if (success(scn::scan(m_contentFilterString, "{}-{}", dataID, range)) || success(scn::scan(m_contentFilterString, "id:{}-{}", dataID, range)) || success(scn::scan(m_contentFilterString, "dataid:{}-{}", dataID, range)))
            m_contentFilterDataID = { dataID, range };
        else if (success(scn::scan_default(m_contentFilterString, dataID)) || success(scn::scan(m_contentFilterString, "id:{}", dataID)) || success(scn::scan(m_contentFilterString, "dataid:{}", dataID)))
            m_contentFilterDataID = { dataID, dataID };
        else if (uint32 uid; success(scn::scan(m_contentFilterString, "uid:{}+{}", uid, range)))
            m_contentFilterUID = { (uint32)std::max(0, (int32)uid - range), uid + range };
        else if (success(scn::scan(m_contentFilterString, "uid:{}-{}", uid, range)))
            m_contentFilterUID = { uid, range };
        else if (success(scn::scan(m_contentFilterString, "uid:{}", uid)))
            m_contentFilterUID = { uid, uid };
        else
            m_contentFilterName = m_contentFilterString;

        asyncContentFilter.Run([delayed, filter = ContentFilter
        {
            .TypeIndex = m_contentFilterType - 1,
            .NameSearch = from_utf8(m_contentFilterName),
            .GUIDSearch = m_contentFilterGUID,
            .UIDSearch = m_contentFilterUID.first != (uint32)-1 ? std::optional(m_contentFilterUID) : std::nullopt,
            .DataIDSearch = m_contentFilterDataID.first != (uint32)-1 ? std::optional(m_contentFilterDataID) : std::nullopt,
        }](AsyncContext context) mutable
        {
            if (delayed)
            {
                for (uint32 delay = 0; delay < 10; ++delay)
                {
                    std::this_thread::sleep_for(50ms);
                    CHECK_ASYNC;
                }
            }

            context->SetIndeterminate();
            while (!g_contentLoaded)
            {
                std::this_thread::sleep_for(50ms);
                CHECK_ASYNC;
            }

            CHECK_ASYNC;
            filter.FilteredNamespaces.resize(g_contentNamespaces.size(), ContentFilter::UNCACHED_RESULT);
            CHECK_ASYNC;
            filter.FilteredObjects.resize(g_rootedContentObjects.size() + g_unrootedContentObjects.size(), ContentFilter::UNCACHED_RESULT);
            CHECK_ASYNC;
            context->SetTotal(filter.FilteredObjects.size() + filter.FilteredNamespaces.size());
            auto passesFilter = overloaded
            {
                [&](ContentNamespace const& ns) { return ns.MatchesFilter(filter); },
                [&](ContentObject const& entry) { return entry.MatchesFilter(filter); },
            };
            uint32 processed = 0;
            for (auto* container : { &g_rootedContentObjects, &g_unrootedContentObjects })
            {
                std::for_each(std::execution::par_unseq, container->begin(), container->end(), [&passesFilter, context, &processed](ContentObject* object)
                {
                    CHECK_SHARED_ASYNC;
                    passesFilter(*object);
                    if (static constexpr uint32 interval = 1000; !(++processed % interval)) // processed will like exhibit race conditions, but we don't really care how accurate it is
                        context->InterlockedIncrement(interval);
                });
            }
            CHECK_ASYNC;
            auto recurseNamespaces = [&passesFilter, context, &processed](ContentNamespace& ns, auto& recurseNamespaces) mutable -> void
            {
                CHECK_ASYNC;
                for (auto&& child : ns.Namespaces)
                    recurseNamespaces(*child, recurseNamespaces);
                CHECK_ASYNC;
                passesFilter(ns);
                if (static constexpr uint32 interval = 1000; !(++processed % interval))
                    context->InterlockedIncrement(interval);
            };
            if (auto* root = g_contentRoot)
                recurseNamespaces(*root, recurseNamespaces);
            
            CHECK_ASYNC;
            std::unique_lock _(contentLock);
            contentFilter = std::move(filter);
            context->Finish();
        });
    };

    static AsyncScheduler asyncConversationFilter { true };
    static auto sortConversations = [](AsyncContext context, std::vector<uint32>& data, ConversationSort sort, bool invert)
    {
        std::scoped_lock _(conversationsLock);
        switch (sort)
        {
            using enum ConversationSort;
            case GenID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a < b ^ invert; });
                break;
            case UID:
                complexSort(data, invert, [](uint32 id) { return conversations.at(id).UID; });
                break;
            case StartingSpeakerName:
                complexSort(data, invert, [](uint32 id) { return conversations.at(id).StartingSpeakerName(); });
                break;
            case StartingStateText:
                complexSort(data, invert, [](uint32 id) { return conversations.at(id).StartingStateText(); });
                break;
            case EncounteredTime:
                complexSort(data, invert, [](uint32 id) { return conversations.at(id).EncounteredTime; });
                break;
            default: std::terminate();
        }
    };
    static auto setConversationsResult = [](AsyncContext context, std::vector<uint32>&& data)
    {
        if (context->Cancelled) return;
        std::unique_lock _(conversationsLock);
        filteredConversations = std::move(data);
        context->Finish();
    };
    auto updateConversationSort = [this]
    {
        asyncConversationFilter.Run([sort = m_conversationSort, invert = m_conversationSortInvert](AsyncContext context)
        {
            context->SetIndeterminate();
            std::vector<uint32> data;
            {
                std::shared_lock _(conversationsLock);
                data = filteredConversations;
            }
            CHECK_ASYNC;
            sortConversations(context, data, sort, invert);
            setConversationsResult(context, std::move(data));
        });
    };
    auto updateConversationSearch = [this]
    {
        bool textSearch = false;
        m_conversationFilterID.reset();
        if (m_conversationFilterString.empty())
            ;
        else if (uint32 id, range; success(scn::scan(m_conversationFilterString, "{}+{}", id, range)))
            m_conversationFilterID.emplace(id - range, id + range);
        else if (success(scn::scan(m_conversationFilterString, "{}-{}", id, range)))
            m_conversationFilterID.emplace(id, range);
        else if (successNumber(scn::scan_default(m_conversationFilterString, id)))
            m_conversationFilterID.emplace(id - m_conversationFilterRange, id + m_conversationFilterRange);
        else if (successNumber(scn::scan(m_conversationFilterString, "0x{:x}", id)))
            m_conversationFilterID.emplace(id - m_conversationFilterRange, id + m_conversationFilterRange);
        else
            textSearch = true;

        asyncConversationFilter.Run([textSearch, filter = m_conversationFilterID, string = m_conversationFilterString, sort = m_conversationSort, invert = m_conversationSortInvert](AsyncContext context) mutable
        {
            context->SetIndeterminate();
            std::vector<uint32> data;
            CHECK_ASYNC;
            if (textSearch)
            {
                std::scoped_lock _(conversationsLock);
                std::wstring const query(std::from_range, from_utf8(string) | std::views::transform(toupper));
                data.assign_range(conversations | std::views::keys | std::views::filter([&query](uint32 id)
                {
                    auto const& conversation = conversations.at(id);
                    for (auto const& state : conversation.States)
                    {
                        if (auto const string = GetNormalizedString(state.TextID).first; string && !string->empty() && string->contains(query))
                            return true;
                        if (auto const string = GetNormalizedString(state.SpeakerNameTextID).first; string && !string->empty() && string->contains(query))
                            return true;
                        for (auto const& transition : state.Transitions)
                            if (auto const string = GetNormalizedString(transition.TextID).first; string && !string->empty() && string->contains(query))
                                return true;
                    }
                    return false;
                }));
            }
            else
            {
                auto limits = filter.value_or(std::pair { std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() });
                std::scoped_lock _(conversationsLock);
                data.assign_range(conversations | std::views::filter([limits](auto const& pair) { return (int32)pair.second.UID >= limits.first && (int32)pair.second.UID <= limits.second; }) | std::views::keys);
            }
            CHECK_ASYNC;
            sortConversations(context, data, sort, invert);
            setConversationsResult(context, std::move(data));
        });
    };

    static AsyncScheduler asyncEventFilter { true };
    static auto sortEvents = [](AsyncContext context, std::vector<EventID>& data, EventSort sort, bool invert)
    {
        std::scoped_lock _(eventsLock);
        switch (sort)
        {
            using enum EventSort;
            case ID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a.UID < b.UID ^ invert; });
                break;
            case Map:
                complexSort(data, invert, [](EventID id) { return events.at(id).Map(); });
                break;
            case Type:
                complexSort(data, invert, [](EventID id) { return events.at(id).Type(); });
                break;
            case Title:
                complexSort(data, invert, [](EventID id) { return events.at(id).Title(); });
                break;
            case EncounteredTime:
                complexSort(data, invert, [](EventID id) { return events.at(id).EncounteredTime(); });
                break;
            default: std::terminate();
        }
    };
    static auto setEventsResult = [](AsyncContext context, std::vector<EventID>&& data)
    {
        if (context->Cancelled) return;
        std::unique_lock _(eventsLock);
        filteredEvents = std::move(data);
        context->Finish();
    };
    auto updateEventSort = [this]
    {
        asyncEventFilter.Run([sort = m_eventSort, invert = m_eventSortInvert](AsyncContext context)
        {
            context->SetIndeterminate();
            std::vector<EventID> data;
            {
                std::shared_lock _(eventsLock);
                data = filteredEvents;
            }
            CHECK_ASYNC;
            sortEvents(context, data, sort, invert);
            setEventsResult(context, std::move(data));
        });
    };
    auto updateEventSearch = [this]
    {
        bool textSearch = false;
        m_eventFilterID.reset();
        if (m_eventFilterString.empty())
            ;
        else if (uint32 id, range; success(scn::scan(m_eventFilterString, "{}+{}", id, range)))
            m_eventFilterID.emplace(id - range, id + range);
        else if (success(scn::scan(m_eventFilterString, "{}-{}", id, range)))
            m_eventFilterID.emplace(id, range);
        else if (successNumber(scn::scan_default(m_eventFilterString, id)))
            m_eventFilterID.emplace(id - m_eventFilterRange, id + m_eventFilterRange);
        else if (successNumber(scn::scan(m_eventFilterString, "0x{:x}", id)))
            m_eventFilterID.emplace(id - m_eventFilterRange, id + m_eventFilterRange);
        else
            textSearch = true;

        asyncEventFilter.Run([textSearch, filter = m_eventFilterID, string = m_eventFilterString, filters = m_eventFilters, sort = m_eventSort, invert = m_eventSortInvert](AsyncContext context) mutable
        {
            context->SetIndeterminate();
            std::vector<EventID> data;
            if (!textSearch)
            {
                auto limits = filter.value_or(std::pair { std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() });
                std::scoped_lock _(eventsLock);
                data.assign_range(events | std::views::keys | std::views::filter([limits](EventID id) { return (int32)id.UID >= limits.first && (int32)id.UID <= limits.second; }));
            }
            else
            {
                std::scoped_lock _(eventsLock);
                data.assign_range(events | std::views::keys);
            }
            CHECK_ASYNC;
            if (!(filters.Normal && filters.Group && filters.Meta && filters.Dungeon && filters.NonEvent))
            {
                std::scoped_lock _(eventsLock);
                std::erase_if(data, [&filters](EventID id)
                {
                    auto const& event = events.at(id);
                    return !(
                        !id.UID && filters.NonEvent ||
                        id.UID && std::ranges::any_of(event.States, &Event::State::IsDungeonEvent) && filters.Dungeon ||
                        id.UID && std::ranges::any_of(event.States, &Event::State::IsMetaEvent) && filters.Meta ||
                        id.UID && std::ranges::any_of(event.States, &Event::State::IsGroupEvent) && filters.Group ||
                        id.UID && std::ranges::any_of(event.States, &Event::State::IsNormalEvent) && filters.Normal
                    );
                });
            }
            CHECK_ASYNC;
            if (textSearch)
            {
                std::scoped_lock _(eventsLock);
                std::wstring const query(std::from_range, from_utf8(string) | std::views::transform(toupper));
                std::erase_if(data, [&query](EventID id)
                {
                    auto const& event = events.at(id);
                    for (auto const& state : event.States)
                    {
                        if (auto const string = GetNormalizedString(state.TitleTextID).first; string && !string->empty() && string->contains(query))
                            return false;
                        for (auto const& param : state.TitleParameterTextID)
                            if (auto const string = GetNormalizedString(param).first; string && !string->empty() && string->contains(query))
                                return false;
                        if (auto const string = GetNormalizedString(state.DescriptionTextID).first; string && !string->empty() && string->contains(query))
                            return false;
                        if (auto const string = GetNormalizedString(state.MetaTextTextID).first; string && !string->empty() && string->contains(query))
                            return false;
                    }
                    for (auto const& objective : event.Objectives)
                    {
                        if (auto const string = GetNormalizedString(objective.TextID).first; string && !string->empty() && string->contains(query))
                            return false;
                        if (auto const string = GetNormalizedString(objective.AgentNameTextID).first; string && !string->empty() && string->contains(query))
                            return false;
                    }
                    if (std::wstring const map { std::from_range, event.Map() | std::views::transform(toupper) }; map.contains(query))
                        return false;
                    return true;
                });
            }
            CHECK_ASYNC;
            sortEvents(context, data, sort, invert);
            setEventsResult(context, std::move(data));
        });
    };
    auto updateEventFilter = updateEventSearch;

    static bool needInitialSettings = g_config.GameExePath.empty() || g_config.GameDatPath.empty();
    static struct
    {
        bool ImGuiDemo = true;
        bool Settings = needInitialSettings;
        bool Notes = false;
        bool Parse = false;
        bool MigrateContentTypes = false;
        bool ContentSearch = false;
        bool Demangle = false;
    } windows;

    if (windows.ImGuiDemo)
    {
        I::SetNextWindowPos(ImVec2(500, 0), ImGuiCond_FirstUseEver);
        I::ShowDemoWindow(&windows.ImGuiDemo);
    }

    if (scoped::Window("Settings", &windows.Settings, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        I::InputText("Gw2-64.exe Path", &g_config.GameExePath);
        I::InputText("Gw2.dat Path", &g_config.GameDatPath);
        I::InputText("Local.dat Path (optional)", &g_config.LocalDatPath);
        I::InputText("Decryption Keys DB (.sqlite/.txt) (optional)", &g_config.DecryptionKeysPath);
        if (scoped::Disabled(g_config.GameExePath.empty() || g_config.GameDatPath.empty()))
        {
            if (I::Button("OK"))
            {
                windows.Settings = false;
                needInitialSettings = false;
            }
        }

        if (needInitialSettings)
            return;
    }

    if (scoped::Window("Notes", &windows.Notes, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        I::SetNextItemWidth(-FLT_MIN);
        I::InputTextMultiline("##Notes", &g_config.Notes, { -1, -1 }, ImGuiInputTextFlags_AllowTabInput);
    }

    if (scoped::Window("Parse", &windows.Parse, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        static auto parse = overloaded
        {
            [](std::string_view in, std::string& out)
            {
                out = in;
                return true;
            },
            [](std::string_view in, GUID& out)
            {
                return success(scn::scan(in, "{}", out));
            },
            [](std::string_view in, pf::Token32& out)
            {
                out = in;
                return true;
            },
            [](std::string_view in, pf::Token64& out)
            {
                out = in;
                return true;
            },
            [](std::string_view in, pf::FileReference& out)
            {
                if (uint32 fileID; success(scn::scan(in, "{}", fileID)))
                {
                    auto* chars = (uint16*)&out;
                    chars[0] = 0xFF + fileID % 0xFF00;
                    chars[1] = 0x100 + fileID / 0xFF00;
                    chars[2] = 0;
                    return true;
                }
                return false;
            },
            [](std::string_view in, std::array<byte, 16>& out)
            {
                return success(scn::scan(in, "{:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x}", out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], out[8], out[9], out[10], out[11], out[12], out[13], out[14], out[15]));
            },
            [](std::string_view in, std::array<uint32, 4>& out)
            {
                return success(scn::scan(in, "{} {} {} {}", out[0], out[1], out[2], out[3])) || success(scn::scan(in, "0x{:x} 0x{:x} 0x{:x} 0x{:x}", out[0], out[1], out[2], out[3]));
            },
            [](std::string_view in, std::array<uint64, 2>& out)
            {
                return success(scn::scan(in, "{} {}", out[0], out[1])) || success(scn::scan(in, "0x{:x} 0x{:x}", out[0], out[1]));
            },
            []<std::integral T>(std::string_view in, T& out)
            {
                if (in.contains(' '))
                {
                    auto type = [&]<typename BufferT>(BufferT)
                    {
                        std::vector<BufferT> tokens;
                        BufferT buffer;
                        auto range = std::ranges::subrange(in);
                        out = { };
                        size_t i = 0;
                        while (auto result = scn::scan(range, "{:02x}", buffer))
                        {
                            out |= (T)buffer << i++ * 8 * sizeof(BufferT);
                            range = result.subrange();
                            if (result.empty() || std::ranges::all_of(range, isspace))
                                return true;
                        }
                        return false;
                    };
                    if (type(byte())) return true;
                    if (type(uint16())) return true;
                }
                else if (success(scn::scan(in, "0x{:x}", out)))
                    return true;
                else if (success(scn::scan(in, "{}", out)))
                    return true;

                return false;
            },
            [](std::string_view in, auto& out) { return false; }
        };
        static auto print = overloaded
        {
            []<std::integral T>(T const& in) { return std::format("{}", in); },
            [](GUID const& in) { return std::format("{}", in); },
            [](pf::Token32 const& in) { return std::format("{}", in.GetString().data()); },
            [](pf::Token64 const& in) { return std::format("{}", in.GetString().data()); },
            [](pf::FileReference const& in) { return std::format("{}", in.GetFileID()); },
            [](std::array<byte, 16> const& in) { return std::format("{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}", in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7], in[8], in[9], in[10], in[11], in[12], in[13], in[14], in[15]); },
            [](std::array<uint32, 4> const& in) { return std::format("0x{:X} 0x{:X} 0x{:X} 0x{:X}", in[0], in[1], in[2], in[3]); },
            [](std::array<uint64, 2> const& in) { return std::format("0x{:X} 0x{:X}", in[0], in[1]); },
            [](std::string_view const& in) { return in; },
            [](std::wstring_view const& in) { return to_utf8(in); },
        };
        static auto conversion = []<ConstString Label, typename L, typename R>(void(*convertLR)(L const& in, R& out), void(*convertRL)(R const& in, L& out) = nullptr)
        {
            static std::string inputL, inputR;
            static L l { };
            static R r { };

            I::TableNextRow();
            I::TableNextColumn();
            I::AlignTextToFramePadding();
            I::TextUnformatted(Label.str);

            if (scoped::WithID(Label.str))
            {
                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                if (I::InputText("##L", &inputL) && parse(inputL, l))
                {
                    convertLR(l, r);
                    inputR = print(r);
                }

                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                if (I::InputText("##R", &inputR, convertRL ? 0 : ImGuiInputTextFlags_ReadOnly) && parse(inputR, r) && convertRL)
                {
                    convertRL(r, l);
                    inputL = print(l);
                }
            }
        };
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("Conversions", 3))
        {
            I::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("Left");
            I::TableSetupColumn("Right");

            conversion.operator()<"uint64", uint64, uint64>([](uint64 const& in, uint64& out) { out = in; }, [](uint64  const&in, uint64& out) { out = in; });
            conversion.operator()<"Token32", uint32, pf::Token32>([](uint32 const& in, pf::Token32& out) { out = *(pf::Token32*)&in; }, [](pf::Token32 const& in, uint32& out) { out = *(uint32*)&in; });
            conversion.operator()<"Token64", uint64, pf::Token64>([](uint64 const& in, pf::Token64& out) { out = *(pf::Token64*)&in; }, [](pf::Token64 const& in, uint64& out) { out = *(uint64*)&in; });
            conversion.operator()<"FileReference", uint64, pf::FileReference>([](uint64 const& in, pf::FileReference& out) { out = *(pf::FileReference*)&in; }, [](pf::FileReference const& in, uint64& out) { out = 0; memcpy(&out, &in, sizeof(in)); });
            conversion.operator()<"Mangle Content Name", std::string, std::wstring>([](std::string const& in, std::wstring& out) { out = Content::MangleFullName(from_utf8(in)); });
            conversion.operator()<"hex[16]<->GUID", std::array<byte, 16>, GUID>([](std::array<byte, 16> const& in, GUID& out) { out = *(GUID const*)in.data(); }, [](GUID const& in, std::array<byte, 16>& out) { out = *(std::array<byte, 16> const*)&in; });
            conversion.operator()<"uint32[4]<->GUID", std::array<uint32, 4>, GUID>([](std::array<uint32, 4> const& in, GUID& out) { out = *(GUID const*)in.data(); }, [](GUID const& in, std::array<uint32, 4>& out) { out = *(std::array<uint32, 4> const*)&in; });
            conversion.operator()<"uint64[2]<->GUID", std::array<uint64, 2>, GUID>([](std::array<uint64, 2> const& in, GUID& out) { out = *(GUID const*)in.data(); }, [](GUID const& in, std::array<uint64, 2>& out) { out = *(std::array<uint64, 2> const*)&in; });
        }
    }

    if (scoped::Window("Migrate Content Types", &windows.MigrateContentTypes, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        static auto mappings = []
        {
            std::vector<uint32> mappings(g_config.LastNumContentTypes);
            std::ranges::iota(mappings, 0);
            uint32 offset = 0;
            for (auto&& [oldIndex, newIndex] : mappings | std::views::enumerate)
            {
                if (auto const& examples = g_config.TypeInfo[oldIndex].Examples; !examples.empty())
                {
                    for (auto const& example : examples)
                    {
                        if (auto const object = GetContentObjectByGUID(example))
                        {
                            offset = object->Type->Index - oldIndex;
                            break;
                        }
                    }
                }
                newIndex += offset;
            }
            return mappings;
        }();

        if (scoped::Disabled(g_config.LastNumContentTypes == g_contentTypeInfos.size()))
        if (I::Button("Migrate"))
        {
            g_config.LastNumContentTypes = g_contentTypeInfos.size();
            decltype(g_config.TypeInfo) newTypeInfo;
            for (auto&& [oldIndex, newIndex] : mappings | std::views::enumerate | std::views::reverse)
            {
                if (auto const itr = g_config.TypeInfo.find(oldIndex); itr != g_config.TypeInfo.end())
                {
                    auto node = g_config.TypeInfo.extract(itr);
                    node.key() = newIndex;
                    newTypeInfo.insert(std::move(node));
                }
            }
            g_config.TypeInfo = std::move(newTypeInfo);
        }
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("Differences", 5))
        {
            I::TableSetupColumn("OldIndex", ImGuiTableColumnFlags_WidthFixed, 30);
            I::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("NewIndex", ImGuiTableColumnFlags_WidthFixed, 30);
            I::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupColumn("ContentType", ImGuiTableColumnFlags_WidthStretch);

            for (auto&& [oldIndex, newIndex] : mappings | std::views::enumerate)
            {
                scoped::WithID(oldIndex);
                bool const changed = oldIndex != newIndex;

                I::TableNextRow();

                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                if (auto index = std::format("{}", oldIndex); scoped::WithColorVar(ImGuiCol_Text, changed ? 0xFF0000FF : I::GetColorU32(ImGuiCol_Text)))
                    I::InputText("##OldIndex", &index, ImGuiInputTextFlags_ReadOnly);

                I::TableNextColumn();
                I::TextUnformatted(ICON_FA_ARROW_RIGHT);

                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                if (auto index = std::format("{}", newIndex); scoped::WithColorVar(ImGuiCol_Text, changed ? 0xFF00FF00 : I::GetColorU32(ImGuiCol_Text)))
                    I::InputText("##NewIndex", &index, ImGuiInputTextFlags_ReadOnly);

                if (auto const itr = g_config.TypeInfo.find(oldIndex); itr != g_config.TypeInfo.end())
                {
                    I::TableNextColumn();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::InputText("##Name", &itr->second.Name);

                    I::TableNextColumn();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::ComboWithFilter("##ContentType", (int*)&itr->second.ContentType, { std::from_range, magic_enum::enum_names<Content::EContentTypes>() | std::views::transform([](std::string_view e) { return std::string(e); }) });
                }
            }
        }
    }

    static struct
    {
        AsyncScheduler Async;
        std::mutex Lock;
        TypeInfo::SymbolType const* Symbol;
        TypeInfo::Condition::ValueType Value;
        std::vector<ContentObject*> Results;

        void SearchForSymbolValue(std::string_view symbolTypeName, TypeInfo::Condition::ValueType value)
        {
            windows.ContentSearch = true;
            Async.Run([this, symbolTypeName, value](AsyncContext context)
            {
                {
                    std::scoped_lock _(Lock);
                    Symbol = Symbols::GetByName(symbolTypeName);
                    Value = value;
                    Results.clear();
                }
                auto _ = scoped_seh_exception_handler::Create();
                context->SetTotal(g_contentObjectsByDataPointer.size());
                std::for_each(std::execution::par_unseq, g_contentObjectsByDataPointer.begin(), g_contentObjectsByDataPointer.end(), [this, context, processed = 0](auto& pair) mutable
                {
                    CHECK_SHARED_ASYNC;
                    try
                    {
                        pair.second->Finalize();
                        if (auto generator = QuerySymbolData(*pair.second, *Symbol, Value); generator.begin() != generator.end())
                        {
                            std::scoped_lock _(Lock);
                            Results.emplace_back(pair.second);
                        }
                    }
                    catch (...) { }
                    if (static constexpr uint32 interval = 1000; !(++processed % interval))
                        context->InterlockedIncrement(interval);
                });
                context->Finish();
            });
        }
    } contentSearch;
    if (scoped::Window("Content Search", &windows.ContentSearch, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        std::scoped_lock __(contentSearch.Lock);
        I::SetNextItemWidth(-FLT_MIN);
        if (scoped::Disabled(true))
            I::InputText("##Description", (char*)std::format("Content that contains {} fields with value: {}", contentSearch.Symbol->Name, contentSearch.Value).c_str(), 9999);
        if (auto context = contentSearch.Async.Current())
        {
            I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
            if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
            if (scoped::WithColorVar(ImGuiCol_Border, 0))
            if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
            if (scoped::WithColorVar(ImGuiCol_Text, 0))
            if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                if (context.IsIndeterminate())
                    I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                else
                    I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
        }
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
        if (scoped::Child("Content", { }, 0, ImGuiWindowFlags_AlwaysVerticalScrollbar))
            for (auto const& object : contentSearch.Results)
                DrawContentButton(object, &object);
    }

    static struct
    {
        AsyncScheduler Async;
        std::mutex Lock;
        std::unordered_multimap<ContentNamespace*, std::wstring> NamespaceResults;
        std::unordered_multimap<ContentObject*, std::wstring> ContentResults;
        std::wstring ResultsPrefix;
        bool BruteforceObjects = false;
        bool BruteforceNamespaces = true;
        bool BruteforceRecursively = false;
        std::deque<ContentNamespace*> BruteforceRecursiveQueue;

        std::mutex UILock;
        std::wstring BruteforceUIPrefix;
        ContentNamespace* BruteforceUIRecursiveBase = nullptr;
        bool BruteforceUIStart = false;

        void MatchNamespace(std::wstring_view fullName, std::wstring_view mangledName, picosha2::hash256_one_by_one& hasher)
        {
            if (auto const itr = g_contentNamespacesByName.find(mangledName); itr != g_contentNamespacesByName.end())
            {
                auto const pos = fullName.find_last_of(L'.');
                auto const name = fullName.substr(pos + 1);
                auto const parentName = fullName.substr(0, pos);
                Content::Mangle(parentName, (wchar_t*)mangledName.data(), 6, hasher);
                std::scoped_lock _(Lock);
                for (auto ns : itr->second)
                    if (ns->Parent && ns->Parent->Name == mangledName.substr(0, 5))
                        if (auto range = NamespaceResults.equal_range(ns); !std::ranges::contains(range.first, range.second, name, &decltype(NamespaceResults)::value_type::second))
                            NamespaceResults.emplace(ns, name);
                windows.Demangle = true;
            }
        }
        void MatchObject(std::wstring_view name, std::wstring_view mangledName)
        {
            if (auto const itr = g_contentObjectsByName.find(mangledName); itr != g_contentObjectsByName.end())
            {
                std::scoped_lock _(Lock);
                for (auto object : itr->second)
                    if (auto range = ContentResults.equal_range(object); !std::ranges::contains(range.first, range.second, name, &decltype(ContentResults)::value_type::second))
                        ContentResults.emplace(object, name);
                windows.Demangle = true;
            }
        }
        void Match(std::wstring_view name, std::wstring_view mangledName)
        {
            auto const namePos = name.find_last_of(L'.');
            if (auto const pos = mangledName.find(L'.'); pos != std::wstring_view::npos)
            {
                picosha2::hash256_one_by_one hasher;
                MatchNamespace(name.substr(0, namePos), mangledName.substr(0, pos), hasher);
                mangledName.remove_prefix(pos + 1);
            }
            MatchObject(name.substr(namePos + 1), mangledName);
        }
        void Match(std::wstring_view name)
        {
            Match(name, Content::MangleFullName(name));
        }
        void MatchRecursively(std::wstring_view name)
        {
            ResultsPrefix.clear();
            while (true)
            {
                Match(name, Content::MangleFullName(name));
                auto const pos = name.find_last_of(L'.');
                if (pos == std::wstring_view::npos)
                    break;
                name = name.substr(0, pos);
            }
        }
        void Bruteforce(std::wstring_view prefix, ContentNamespace* recursiveBase, uint32 words)
        {
            Async.Run([this, nonRecursivePrefix = std::wstring(prefix), recursiveBase, words, objects = BruteforceObjects, namespaces = BruteforceNamespaces, recursively = BruteforceRecursively](AsyncContext context)
            {
                context->SetIndeterminate();
                {
                    std::scoped_lock _(Lock);
                    BruteforceRecursiveQueue.clear();
                }

                std::set<std::wstring> uniqueDictionary;
                //dictionary.reserve(std::ranges::count(g_config.BruteforceDictionary, L'\n') + 1);
                for (auto const& word : g_config.BruteforceDictionary | std::views::split(L'\n'))
                    uniqueDictionary.emplace(std::from_range, word);
                uniqueDictionary.erase(L"");

                std::vector<std::wstring> dictionary { std::from_range, uniqueDictionary };

                /* Decent optimization, but ultimately meaningless considering that the bulk of the time is spent elsewhere
                static auto nameHashInUse = []
                {
                    std::wstring paddedName(8, L'=');
                    auto result = std::make_unique<std::bitset<1 << 30>>();
                    for (auto const& name : g_contentNamespacesByName | std::views::keys)
                    {
                        name.copy(paddedName.data(), name.size());
                        result->set(std::byteswap(pf::DemangleToNumber(paddedName)) >> 2);
                    }
                    return result;
                }();
                */

                auto process = [this, &dictionary, objects, namespaces](AsyncContext context, std::span<wchar_t> name, uint32 depth, std::span<wchar_t> mangledBuffer, picosha2::hash256_one_by_one& hasher, auto& generate_combinations) -> void
                {
                    if (!depth)
                    {
                        std::wstring_view nameView { name.data(), name.size() };
                        if (namespaces)
                        {
                            //if (nameHashInUse->test(std::byteswap((uint32)pf::MangleToNumber(nameView)) >> 2))
                            {
                                Content::Mangle(nameView, mangledBuffer.data(), 6, hasher);
                                MatchNamespace(nameView, { mangledBuffer.data(), 5 }, hasher);
                            }
                        }
                        if (objects)
                        {
                            nameView.remove_prefix(nameView.find_last_of(L'.') + 1);
                            //if (nameHashInUse->test(std::byteswap((uint32)pf::MangleToNumber(nameView)) >> 2))
                            {
                                Content::Mangle(nameView, mangledBuffer.data(), 6, hasher);
                                MatchObject(nameView, { mangledBuffer.data(), 5 });
                            }
                        }
                        return;
                    }

                    for (const auto& word : dictionary)
                    {
                        CHECK_ASYNC;
                        auto const begin = name.begin();
                        auto end = name.end();
                        auto wordEnd = end + word.copy(&*end, 4096);
                        // PrefixWord
                        generate_combinations(context, { begin, wordEnd }, depth - 1, mangledBuffer, hasher, generate_combinations);
                        // PrefixWords
                        *wordEnd++ = L's';
                        generate_combinations(context, { begin, wordEnd }, depth - 1, mangledBuffer, hasher, generate_combinations);
                        // Prefix Word
                        *end++ = L' ';
                        wordEnd = end + word.copy(&*end, 4096);
                        generate_combinations(context, { begin, wordEnd }, depth - 1, mangledBuffer, hasher, generate_combinations);
                        // Prefix Words
                        *wordEnd++ = L's';
                        generate_combinations(context, { begin, wordEnd }, depth - 1, mangledBuffer, hasher, generate_combinations);
                    }
                };

                auto generatePrefix = [this, nonRecursivePrefix, objects, namespaces, recursively](ContentNamespace const& ns, bool skipRecursiveQueue, auto& generatePrefix) -> std::experimental::generator<std::wstring>
                {
                    if (!recursively)
                    {
                        co_yield nonRecursivePrefix;
                        co_return;
                    }

                    while (!skipRecursiveQueue)
                    {
                        ContentNamespace* current = nullptr;
                        {
                            std::scoped_lock _(Lock);
                            if (!BruteforceRecursiveQueue.empty())
                            {
                                current = BruteforceRecursiveQueue.front();
                                BruteforceRecursiveQueue.pop_front();
                            }
                        }

                        if (!current)
                            break;

                        for (auto const& result : generatePrefix(*current, true, generatePrefix))
                            co_yield result;
                    }

                    std::wstring current;
                    {
                        std::scoped_lock _(Lock);
                        if (objects && std::ranges::any_of(ns.Entries, [](auto const& object) { return !object->HasCustomName(); }) ||
                            namespaces && std::ranges::any_of(ns.Namespaces, [](auto const& ns) { return !ns->HasCustomName(); }))
                            current = std::format(L"{}.", ns.GetFullDisplayName());
                    }
                    if (!current.empty())
                        co_yield current;

                    for (auto const& child : ns.Namespaces)
                    {
                        bool named;
                        {
                            std::scoped_lock _(Lock);
                            named = child->HasCustomName();
                        }
                        if (named)
                            for (auto const& result : generatePrefix(*child, false, generatePrefix))
                                co_yield result;
                    }
                };

                context->SetTotal(dictionary.size());
                for (auto const& prefix : generatePrefix(recursiveBase ? *recursiveBase : *g_contentRoot, false, generatePrefix))
                {
                    CHECK_ASYNC;
                    context->Clear();
                    {
                        std::scoped_lock _(UILock);
                        ResultsPrefix = prefix; // TODO: Store alongside results
                        BruteforceUIPrefix = prefix;
                    }
                    std::for_each(std::execution::par_unseq, dictionary.begin(), dictionary.end(), [context, &process, prefix, words](std::wstring const& word) mutable
                    {
                        CHECK_ASYNC;
                        std::wstring mangledBuffer(Content::MANGLE_FULL_NAME_BUFFER_SIZE, L'\0');
                        std::wstring nameBuffer(4096, L'\0');
                        auto const begin = nameBuffer.begin();
                        auto const end = begin + prefix.copy(nameBuffer.data(), nameBuffer.size());
                        picosha2::hash256_one_by_one hasher;
                        for (uint32 depth = 0; depth < words; ++depth)
                        {
                            CHECK_ASYNC;
                            auto wordEnd = end + word.copy(&*end, 4096);
                            process(context, { begin, wordEnd }, depth, mangledBuffer, hasher, process);
                            *wordEnd++ = L's';
                            process(context, { begin, wordEnd }, depth, mangledBuffer, hasher, process);
                        }
                        CHECK_ASYNC;
                        context->InterlockedIncrement();
                    });
                    CHECK_ASYNC;
                }
                context->Finish();
            });
        }
        void OpenBruteforceUI(std::wstring_view prefix, ContentNamespace* recursiveBase, bool start = false, std::optional<bool> objects = { }, std::optional<bool> namespaces = { })
        {
            BruteforceUIPrefix = prefix;
            BruteforceUIRecursiveBase = recursiveBase;
            BruteforceUIStart = start;
            if (objects)
                BruteforceObjects = *objects;
            if (namespaces)
                BruteforceNamespaces = *namespaces;
            windows.Demangle = true;
        }
    } demangle;
    if (scoped::Window("Demangle", &windows.Demangle, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        std::scoped_lock __(demangle.UILock);
        if (scoped::TabBar("Tabs"))
        {
            if (scoped::TabItem("Match Name List"))
            {
                static std::string names;
                I::SetNextItemWidth(-FLT_MIN);
                if (I::InputTextMultiline("##Names", &names, { -FLT_MIN, 100 }))
                    for (auto const& name : std::views::split(names, '\n'))
                        demangle.MatchRecursively(from_utf8(std::string(std::from_range, name)));
            }
            if (scoped::TabItem("Bruteforce", nullptr, !demangle.BruteforceUIPrefix.empty() ? ImGuiTabItemFlags_SetSelected : 0))
            {
                static std::string dictionary = to_utf8(g_config.BruteforceDictionary);
                I::SetNextItemWidth(-FLT_MIN);
                if (I::InputTextMultiline("##Dictionary", &dictionary, { -FLT_MIN, 100 }))
                    g_config.BruteforceDictionary = from_utf8(dictionary);

                static std::string prefix;
                if (!demangle.BruteforceUIPrefix.empty())
                {
                    prefix = to_utf8(demangle.BruteforceUIPrefix);
                    demangle.BruteforceUIPrefix.clear();
                }
                I::SetNextItemWidth(-200);
                I::InputText("##Prefix", &prefix);
                if (auto context = demangle.Async.Current())
                {
                    I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
                    if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
                    if (scoped::WithColorVar(ImGuiCol_Border, 0))
                    if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
                    if (scoped::WithColorVar(ImGuiCol_Text, 0))
                    if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                        if (context.IsIndeterminate())
                            I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                        else
                            I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
                }
                I::SameLine();

                static int words = 2;
                I::SetNextItemWidth(70);
                I::DragInt("##Words", &words, 0.02f, 1, 100, "Words: %u");
                I::SameLine();

                I::CheckboxButton(ICON_FA_FILE, demangle.BruteforceObjects, "Bruteforce Content Object Names", I::GetFrameHeight());
                I::SameLine(0, 0);
                I::CheckboxButton(ICON_FA_FOLDER, demangle.BruteforceNamespaces, "Bruteforce Content Namespace Names", I::GetFrameHeight());
                I::SameLine();

                I::CheckboxButton(ICON_FA_FOLDER_TREE, demangle.BruteforceRecursively, "Bruteforce Recursively", I::GetFrameHeight());
                I::SameLine();

                if (demangle.Async.Current() && !demangle.BruteforceUIStart)
                {
                    if (I::Button("Stop", { I::GetContentRegionAvail().x, 0 }))
                        demangle.Async.Run([](AsyncContext context) { context->Finish(); });
                }
                else if (I::Button("Start", { I::GetContentRegionAvail().x, 0 }) || demangle.BruteforceUIStart)
                {
                    bool const old = demangle.BruteforceRecursively;
                    if (demangle.BruteforceUIRecursiveBase)
                        demangle.BruteforceRecursively = true;
                    demangle.Bruteforce(from_utf8(prefix), demangle.BruteforceUIRecursiveBase, words);
                    if (demangle.BruteforceUIRecursiveBase)
                        demangle.BruteforceRecursively = old;
                }
                demangle.BruteforceUIStart = false;
                demangle.BruteforceUIRecursiveBase = nullptr;
            }
        }

        std::scoped_lock ___(demangle.Lock);
        bool apply = false;
        if (I::Button("Apply"))
            apply = true;
        I::SameLine();
        if (I::Button("Clear"))
        {
            demangle.NamespaceResults.clear();
            demangle.ContentResults.clear();
        }
        I::SameLine();
        static bool onlyUnnamed;
        I::Checkbox("Only Unnamed", &onlyUnnamed);
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
        if (scoped::Table("Results", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, { -FLT_MIN, -FLT_MIN }))
        {
            I::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            I::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150);
            I::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupScrollFreeze(0, 1);
            I::TableHeadersRow();

            picosha2::hash256_one_by_one hasher;
            static std::wstring mangledPrefix(Content::MANGLE_FULL_NAME_BUFFER_SIZE, L'\0');
            Content::MangleFullName(demangle.ResultsPrefix, mangledPrefix.data(), mangledPrefix.size(), hasher);

            static auto filter = overloaded
            {
                [](ContentNamespace const* ns, std::wstring const& name)
                {
                    if (auto const itr = g_config.ContentNamespaceNames.find(ns->GetFullName()); itr != g_config.ContentNamespaceNames.end() && (onlyUnnamed || itr->second == name))
                        return true;
                    return false;
                },
                [](ContentObject const* object, std::wstring const& name)
                {
                    if (auto const itr = g_config.ContentObjectNames.find(*object->GetGUID()); itr != g_config.ContentObjectNames.end() && (onlyUnnamed || itr->second == name))
                        return true;
                    if (!demangle.ResultsPrefix.empty())
                        if (!object->GetFullName().starts_with({ mangledPrefix.data(), 5 }))
                            return true;
                    return false;
                }
            };
            std::erase_if(demangle.NamespaceResults, [](auto const& pair) { return filter(pair.first, pair.second); });
            std::erase_if(demangle.ContentResults, [](auto const& pair) { return filter(pair.first, pair.second); });
            std::optional<decltype(demangle.NamespaceResults)::value_type> eraseNamespace;
            std::optional<decltype(demangle.ContentResults)::value_type> eraseObject;
            ImGuiListClipper clipper;
            clipper.Begin(demangle.NamespaceResults.size() + demangle.ContentResults.size(), I::GetFrameHeight());
            while (clipper.Step())
            {
                int drawn = 0;
                for (auto const& [ns, name] : demangle.NamespaceResults | std::views::drop(clipper.DisplayStart) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart))
                {
                    if (filter(ns, name))
                        continue;

                    scoped::WithID(&ns);
                    I::TableNextRow();

                    I::TableNextColumn();
                    if (I::Button("<c=#0F0>" ICON_FA_CHECK "</c>") || apply)
                    {
                        g_config.ContentNamespaceNames[ns->GetFullName()] = name;
                        demangle.BruteforceRecursiveQueue.emplace_back(ns);
                    }
                    I::SameLine(0, 0);
                    if (I::Button("<c=#F00>" ICON_FA_XMARK "</c>"))
                        eraseNamespace.emplace(ns, name);

                    I::TableNextColumn();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::InputTextReadOnly("##Name", to_utf8(name));

                    I::TableNextColumn();
                    I::Text(ICON_FA_FOLDER_CLOSED " %s", to_utf8(ns->GetFullDisplayName()).c_str());
                    ++drawn;
                }
                for (auto const& [object, name] : demangle.ContentResults | std::views::drop(std::max(0, clipper.DisplayStart - (int)demangle.NamespaceResults.size())) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart - drawn))
                {
                    if (filter(object, name))
                        continue;

                    scoped::WithID(&object);
                    I::TableNextRow();

                    I::TableNextColumn();
                    if (I::Button("<c=#0F0>" ICON_FA_CHECK "</c>") || apply)
                        g_config.ContentObjectNames[*object->GetGUID()] = name;
                    I::SameLine(0, 0);
                    if (I::Button("<c=#F00>" ICON_FA_XMARK "</c>"))
                        eraseObject.emplace(object, name);

                    I::TableNextColumn();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::InputTextReadOnly("##Name", to_utf8(name));

                    I::TableNextColumn();
                    DrawContentButton(object, object);
                }
            }
            if (eraseNamespace)
                demangle.NamespaceResults.erase(std::ranges::find(demangle.NamespaceResults, *eraseNamespace));
            if (eraseObject)
                demangle.ContentResults.erase(std::ranges::find(demangle.ContentResults, *eraseObject));
        }
    }

    if (g_config.MainWindowFullScreen)
    {
        I::SetNextWindowPos(I::GetMainViewport()->WorkPos);
        I::SetNextWindowSize(I::GetMainViewport()->WorkSize);
    }
    if (scoped::Window("GW2Browser", nullptr, ImGuiWindowFlags_MenuBar | (g_config.MainWindowFullScreen ? ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove : 0)))
    {
        if (scoped::MenuBar())
        {
            if (scoped::Menu("Config"))
            {
                if (I::MenuItem("Load"))
                    g_config.Load();
                if (I::MenuItem("Save"))
                    g_config.Save();
            }
            if (scoped::Menu("View"))
            {
                I::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
                if (I::MenuItem("Show Original Names", nullptr, &m_showOriginalNames))
                    ClearContentSortCache();
                I::MenuItem("Show <c=#CCF>Valid Raw Pointers</c>", nullptr, &g_config.ShowValidRawPointers);
                I::MenuItem("Show Content Symbol <c=#8>Name</c> Before <c=#4>Type</c>", nullptr, &g_config.ShowContentSymbolNameBeforeType);
                I::MenuItem("Display Content Layout As  " ICON_FA_FOLDER_TREE " Tree", nullptr, &g_config.TreeContentStructLayout);
                I::MenuItem("Full Screen Window", nullptr, &g_config.MainWindowFullScreen);
                I::MenuItem("Open ImGui Demo Window", nullptr, &windows.ImGuiDemo);
                I::MenuItem("Open Parse Window", nullptr, &windows.Parse);
                I::MenuItem("Open Demangle Window", nullptr, &windows.Demangle);
                I::MenuItem("Open Notes Window", nullptr, &windows.Notes);
                I::MenuItem("Open Settings Window", nullptr, &windows.Settings);
                I::PopItemFlag();
            }
            if (scoped::Menu("Language"))
            {
                static constexpr std::pair<Language, char const*> languages[]
                {
                    { Language::English, "English" },
                    { Language::Korean, "Korean" },
                    { Language::French, "French" },
                    { Language::German, "German" },
                    { Language::Spanish, "Spanish" },
                    { Language::Chinese, "Chinese" },
                };
                I::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
                for (auto const& [lang, text] : languages)
                    if (I::MenuItem(text, nullptr, g_config.Language == lang))
                        g_config.Language = lang;
                I::PopItemFlag();
            }
            if (scoped::Menu("Tools"))
            {
                if (I::MenuItem("Export Content Files"))
                    for (uint32 fileID = g_firstContentFileID, maxID = fileID + g_numContentFiles; fileID < maxID; ++fileID)
                        if (auto data = g_archives.GetFile(fileID); !data.empty())
                            ExportData(data, std::format(R"(Export\Game Content\{}.cntc)", fileID));
                I::MenuItem("Migrate Content Types", nullptr, &windows.MigrateContentTypes);
            }
            for (auto const& progress : m_progress)
            {
                if (auto lock = progress.Lock(); progress.IsRunning())
                {
                    if (progress.IsIndeterminate())
                    {
                        I::SetCursorPosY(I::GetCursorPosY() + 2);
                        I::IndeterminateProgressBar({ 100, 16 });
                        I::SameLine();
                        I::TextUnformatted(progress.GetDescription().c_str());
                    }
                    else
                    {
                        auto [p, current, total] = progress.GetProgress();
                        I::SetCursorPosY(I::GetCursorPosY() + 2);
                        I::ProgressBar(p, { 100, 16 });
                        I::SameLine();
                        I::Text("%zu / %zu", current, total);
                        I::SameLine();
                        I::TextUnformatted(progress.GetDescription().c_str());
                    }
                }
            }
        }
        if (scoped::Child("SourcesPane", { 250, 0 }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX))
        {
            if (scoped::TabBar("Tabs", ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
            {
                if (scoped::TabItem(ICON_FA_FILE " Files"))
                {
                    I::SetNextItemWidth(-60);
                    if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &m_fileFilterString, ImGuiInputTextFlags_CharsDecimal))
                    {
                        if (!success(scn::scan_default(m_fileFilterString, m_fileFilterID)))
                            m_fileFilterID = 0;
                        updateFileSearch();
                    }
                    I::SameLine();
                    I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
                    if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&m_fileFilterRange, 0.1f, 0, 10000))
                        updateFileSearch();
                    if (I::IsItemHovered())
                        I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

                    if (I::SetNextItemWidth(-FLT_MIN); scoped::Combo("##Type", "Any Type"))
                        updateFileFilter();

                    if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
                    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, I::GetStyle().ItemSpacing / 2))
                    if (scoped::Table("Table", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX))
                    {
                        I::TableSetupColumn("File ID", ImGuiTableColumnFlags_WidthStretch);
                        I::TableSetupColumn("Archive", ImGuiTableColumnFlags_WidthStretch);
                        ImGuiListClipper clipper;
                        clipper.Begin(searchedFiles.size());
                        while (clipper.Step())
                        {
                            for (ArchiveFile const& file : std::span(searchedFiles.begin() + clipper.DisplayStart, searchedFiles.begin() + clipper.DisplayEnd))
                            {
                                I::TableNextRow();
                                I::TableNextColumn(); I::Selectable(std::format("{}", file.ID).c_str(), IsViewerRawFile(m_currentViewer, file), ImGuiSelectableFlags_SpanAllColumns);
                                if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                                    OpenFile(file, button & ImGuiButtonFlags_MouseButtonMiddle);
                                if (scoped::PopupContextItem())
                                {
                                    if (I::Button("Search for Content References"))
                                        contentSearch.SearchForSymbolValue("FileID", file.ID);
                                }
                                I::TableNextColumn(); I::Text("<c=#4>%s</c>", file.Source.get().Path.filename().string().c_str());
                            }
                        }
                    }
                }
                if (static bool focus = true; scoped::TabItem(ICON_FA_TEXT " Strings", nullptr, std::exchange(focus, false) ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    I::SetNextItemWidth(-(I::GetStyle().ItemSpacing.x + I::GetFrameHeight() + I::GetFrameHeight() + I::GetStyle().ItemSpacing.x + 60));
                    if (static bool focus = true; std::exchange(focus, false))
                        I::SetKeyboardFocusHere();
                    if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &m_stringFilterString))
                        updateStringSearch();
                    if (auto context = asyncStringFilter.Current())
                    {
                        I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
                        if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
                        if (scoped::WithColorVar(ImGuiCol_Border, 0))
                        if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
                        if (scoped::WithColorVar(ImGuiCol_Text, 0))
                        if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                            if (context.IsIndeterminate())
                                I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                            else
                                I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
                    }
                    I::SameLine();
                    static bool trackClipboard = false;
                    static auto trackClipboardCooldown = now;
                    static std::string previousClipboardContents;
                    if (I::CheckboxButton(ICON_FA_CLIPBOARD, trackClipboard, "Track Clipboard", I::GetFrameHeight()) && trackClipboard)
                        previousClipboardContents = I::GetClipboardText();
                    if (trackClipboard && now >= trackClipboardCooldown)
                    {
                        trackClipboardCooldown = now + 100ms;
                        if (auto clipboard = I::GetClipboardText(); clipboard && previousClipboardContents != clipboard)
                        {
                            m_stringFilterString = previousClipboardContents = clipboard;
                            updateStringSearch();
                        }
                    }
                    I::SameLine(0, 0);
                    static bool copySingleResult = false;
                    std::string singleResult;
                    if (std::shared_lock __(stringsLock); filteredStrings.size() == 1)
                        if (auto [string, status] = GetString(filteredStrings.front()); string)
                            singleResult = to_utf8(*string);
                    static std::string previousSingleResult;
                    if (I::CheckboxButton(ICON_FA_COPY, copySingleResult, "Auto Copy Single Result", I::GetFrameHeight()) && copySingleResult && !singleResult.empty())
                        previousSingleResult = singleResult;
                    if (copySingleResult && !singleResult.empty() && previousSingleResult != singleResult)
                        I::SetClipboardText((previousClipboardContents = previousSingleResult = singleResult).c_str());
                    I::SameLine();
                    if (scoped::Disabled(!m_stringFilterID))
                    {
                        I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
                        if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&m_stringFilterRange, 0.1f, 0, 10000))
                            updateStringSearch();
                        if (I::IsItemHovered())
                            I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    }

                    auto filter = [&, next = false](std::string_view text, bool& filter) mutable
                    {
                        if (std::exchange(next, true))
                            I::SameLine();
                        if (I::Button(std::format("<c=#{}><c=#8>{}</c> {}</c>###StringFilter{}", filter ? "F" : "4", ICON_FA_FILTER, text, text).c_str()))
                        {
                            filter ^= true;
                            updateStringFilter();
                        }
                    };
                    filter("Unencrypted", m_stringFilters.Unencrypted);
                    filter("Encrypted", m_stringFilters.Encrypted);
                    filter("Decrypted", m_stringFilters.Decrypted);
                    filter("Empty", m_stringFilters.Empty);

                    if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
                    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, I::GetStyle().ItemSpacing / 2))
                    if (scoped::Table("Table", 4, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable))
                    {
                        I::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50, (ImGuiID)StringSort::ID);
                        I::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch, 0, (ImGuiID)StringSort::Text);
                        I::TableSetupColumn("Decrypted", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80, (ImGuiID)StringSort::DecryptionTime);
                        I::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80, (ImGuiID)StringSort::Voice);
                        I::TableSetupScrollFreeze(0, 1);
                        I::TableHeadersRow();

                        if (auto specs = I::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0)
                        {
                            m_stringSort = (StringSort)specs->Specs[0].ColumnUserID;
                            m_stringSortInvert = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                            specs->SpecsDirty = false;
                            updateStringSort();
                        }

                        std::shared_lock __(stringsLock);
                        ImGuiListClipper clipper;
                        clipper.Begin(filteredStrings.size());
                        while (clipper.Step())
                        {
                            for (auto stringID : std::span(filteredStrings.begin() + clipper.DisplayStart, filteredStrings.begin() + clipper.DisplayEnd))
                            {
                                scoped::WithID(stringID);

                                auto info = GetDecryptionKeyInfo(stringID);
                                auto [string, status] = GetString(stringID);
                                I::TableNextRow();
                                I::TableNextColumn();
                                I::SetNextItemAllowOverlap();
                                I::Selectable(std::format("{}", stringID).c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                                if (scoped::PopupContextItem())
                                {
                                    static uint64 decryptionKey = 0;
                                    I::Text("Text: %s%s", GetEncryptionStatusText(status), string ? to_utf8(*string).c_str() : "");

                                    DrawCopyButton("ID", stringID);
                                    I::SameLine();
                                    DrawCopyButton("DataLink", MakeDataLink(0x03, 0x100 + stringID));
                                    I::SameLine();
                                    DrawCopyButton("Text", string ? *string : L"", string);

                                    if (I::InputScalar("Decryption Key", ImGuiDataType_U64, info ? &info->Key : &decryptionKey))
                                    {
                                        if (!info)
                                            info = AddDecryptionKeyInfo(stringID, { .Key = std::exchange(decryptionKey, 0) });
                                        WipeStringCache(stringID);
                                    }

                                    if (I::Button("Search for Content References"))
                                        contentSearch.SearchForSymbolValue("StringID", stringID);
                                }

                                I::TableNextColumn();
                                std::string text = string ? to_utf8(*string).c_str() : "";
                                replace_all(text, "\r", R"(<c=#F00>\r</c>)");
                                replace_all(text, "\n", R"(<c=#F00>\n</c>)");
                                I::Text("%s%s", GetEncryptionStatusText(status), text.c_str());

                                I::TableNextColumn();
                                if (info)
                                {
                                    if (I::Selectable(std::format("<c=#{}>{}</c> {}###DecryptionTime", info->Map ? "F" : "2", ICON_FA_GLOBE, format_duration_colored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - std::chrono::system_clock::from_time_t(info->Time)))).c_str()))
                                    {
                                        // TODO: Open map to { info->Map, info->Position }
                                    }
                                    if (scoped::ItemTooltip())
                                        I::TextUnformatted(std::format("Decrypted on: {:%F %T}", std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(std::chrono::system_clock::from_time_t(info->Time)))).c_str());
                                }

                                I::TableNextColumn();
                                DrawTextVoiceButton(stringID, { .Selectable = true });
                            }
                        }
                    }
                }
                if (scoped::TabItem(ICON_FA_FOLDER_TREE " Content"))
                {
                    bool expandAll = false;
                    bool collapseAll = false;
                    I::SetNextItemWidth(-FLT_MIN);
                    if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &m_contentFilterString))
                    {
                        demangle.MatchRecursively(from_utf8(m_contentFilterString));
                        updateContentFilter(true);
                    }
                    if (auto context = asyncContentFilter.Current())
                    {
                        I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
                        if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
                        if (scoped::WithColorVar(ImGuiCol_Border, 0))
                        if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
                        if (scoped::WithColorVar(ImGuiCol_Text, 0))
                        if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                            if (context.IsIndeterminate())
                                I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                            else
                                I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
                    }
                    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
                    if (scoped::Table("Filter", 3, ImGuiTableFlags_NoSavedSettings))
                    {
                        I::TableSetupColumn("Type");
                        I::TableSetupColumn("Expand", ImGuiTableColumnFlags_WidthFixed);
                        I::TableSetupColumn("Collapse", ImGuiTableColumnFlags_WidthFixed);
                        I::TableNextColumn();
                        I::SetNextItemWidth(-FLT_MIN);
                        std::vector<std::string> items;
                        items.reserve(g_contentTypeInfos.size() + 1);
                        items.emplace_back(std::format("<c=#8>{0} {2}</c>", ICON_FA_FILTER, -1, "Any Type"));
                        items.append_range(g_contentTypeInfos | std::views::transform([](auto const& type)
                        {
                            auto const itr = g_config.TypeInfo.find(type->Index);
                            return std::format("<c=#8>{0}</c> {2}  <c=#4>#{1}</c>", ICON_FA_FILTER, type->Index, itr != g_config.TypeInfo.end() && !itr->second.Name.empty() ? itr->second.Name : "");
                        }));
                        if (I::ComboWithFilter("##Type", &m_contentFilterType, items))
                            updateContentFilter();
                        if (I::TableNextColumn(); I::Button(ICON_FA_FOLDER_OPEN))
                            expandAll = true;
                        I::SetItemTooltip("Expand All Namespaces");
                        if (I::TableNextColumn(); I::Button(ICON_FA_FOLDER_CLOSED))
                            collapseAll = true;
                        I::SetItemTooltip("Collapse All Namespaces");
                    }
                    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, { I::GetStyle().FramePadding.x, 0 }))
                    if (scoped::WithStyleVar(ImGuiStyleVar_IndentSpacing, 16))
                    if (scoped::Table("Table", 7, ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable))
                    {
                        I::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide, 0, (ImGuiID)ContentSort::Name);
                        I::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40, (ImGuiID)ContentSort::Type);
                        I::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 40);
                        I::TableSetupColumn("Data ID", ImGuiTableColumnFlags_WidthFixed, 40, (ImGuiID)ContentSort::DataID);
                        I::TableSetupColumn("UID", ImGuiTableColumnFlags_WidthFixed, 40, (ImGuiID)ContentSort::UID);
                        I::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 40, (ImGuiID)ContentSort::GUID);
                        I::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 40);
                        I::TableSetupScrollFreeze(0, 1);
                        I::TableHeadersRow();

                        if (auto specs = I::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0)
                        {
                            m_contentSort = (ContentSort)specs->Specs[0].ColumnUserID;
                            m_contentSortInvert = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                            specs->SpecsDirty = false;
                            updateContentSort();
                        }

                        std::shared_lock __(contentLock);
                        auto passesFilter = overloaded
                        {
                            [&](ContentNamespace const& ns) { return contentFilter.FilteredNamespaces.empty() || ns.MatchesFilter(contentFilter); },
                            [&](ContentObject const& entry) { return contentFilter.FilteredObjects.empty() || entry.MatchesFilter(contentFilter); },
                        };

                        // Virtualizing tree
                        int focusedParentNamespaceIndex = -1;
                        ImGuiListClipper clipper;
                        auto navigateLeft = [&]
                        {
                            auto& g = *I::GetCurrentContext();
                            return clipper.ItemsCount && g.NavMoveScoringItems && g.NavWindow && g.NavWindow->RootWindowForNav == g.CurrentWindow->RootWindowForNav && g.NavMoveClipDir == ImGuiDir_Left;
                        };
                        auto storeFocusedParentInfo = [&](int parentIndex, std::optional<ImGuiID> id = { })
                        {
                            if (!clipper.ItemsCount && focusedParentNamespaceIndex == -1 && id ? I::GetFocusID() == *id : I::IsItemFocused())
                                focusedParentNamespaceIndex = parentIndex;
                        };
                        int virtualIndex = 0;
                        auto processNamespace = [&](ContentNamespace& ns, int parentNamespaceIndex, auto& processNamespace) -> void
                        {
                            if (!passesFilter(ns))
                                return;

                            if (expandAll) I::SetNextItemOpen(true);
                            if (collapseAll) I::SetNextItemOpen(false);

                            bool open;
                            int namespaceIndex;
                            if (auto const index = namespaceIndex = virtualIndex++; index >= clipper.DisplayStart && index < clipper.DisplayEnd || navigateLeft() && namespaceIndex == focusedParentNamespaceIndex)
                            {
                                static constexpr char const* DOMAINS[] { "System", "Game", "Common", "Template", "World", "Continent", "Region", "Map", "Section", "Tool" };
                                I::TableNextRow();
                                I::TableNextColumn(); I::SetNextItemAllowOverlap(); open = I::TreeNodeEx(&ns, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NavLeftJumpsBackHere, ICON_FA_FOLDER " %s", to_utf8(ns.GetDisplayName(m_showOriginalNames)).c_str());
                                storeFocusedParentInfo(parentNamespaceIndex);
                                if (navigateLeft() && namespaceIndex == focusedParentNamespaceIndex)
                                    I::NavMoveRequestResolveWithLastItem(&I::GetCurrentContext()->NavMoveResultLocal);
                                if (I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft) && I::GetIO().KeyCtrl)
                                    demangle.OpenBruteforceUI(std::format(L"{}.", ns.GetFullDisplayName()), nullptr, true);
                                if (scoped::PopupContextItem())
                                {
                                    I::Text("Full Name: %s", to_utf8(ns.GetFullName()).c_str());
                                    I::InputTextUTF8("Name", g_config.ContentNamespaceNames, ns.GetFullName(), ns.Name);

                                    DrawCopyButton("Mangled Name", ns.Name);
                                    I::SameLine();
                                    DrawCopyButton("Full Mangled Name", ns.GetFullName());

                                    DrawCopyButton("Name", ns.GetDisplayName(m_showOriginalNames, true));
                                    I::SameLine();
                                    DrawCopyButton("Full Name", ns.GetFullDisplayName(m_showOriginalNames, true));

                                    I::AlignTextToFramePadding();
                                    I::TextUnformatted("Bruteforce Demangle Name:");
                                    I::SameLine();
                                    if (ns.Parent && I::Button("This"))
                                        demangle.OpenBruteforceUI(std::format(L"{}.", ns.Parent->GetFullDisplayName()), nullptr, true, false, true);
                                    I::SameLine();
                                    if (I::Button("Children"))
                                        demangle.OpenBruteforceUI(std::format(L"{}.", ns.GetFullDisplayName()), nullptr, true, false, true);
                                    I::SameLine();
                                    if (I::Button("Recursively"))
                                        demangle.OpenBruteforceUI(std::format(L"{}.", ns.GetFullDisplayName()), &ns, true, false, true);
                                }
                                if (open && contentFilter && (!contentFilter.FilteredNamespaces.empty() || !contentFilter.FilteredObjects.empty()))
                                {
                                    I::SameLine();
                                    if (std::ranges::any_of(ns.Namespaces, [](auto const& child) { return !contentFilter.FilteredNamespaces[child->Index]; }) ||
                                        std::ranges::any_of(ns.Entries, [](auto const& child) { return !contentFilter.FilteredObjects[child->Index]; }))
                                    {
                                        if (I::Button("<c=#4>" ICON_FA_FILTER_SLASH "</c>"))
                                        {
                                            auto const recurse = I::GetIO().KeyShift;
                                            auto process = overloaded
                                            {
                                                [recurse](ContentNamespace& parent, auto& process) -> void
                                                {
                                                    for (auto const& child : parent.Namespaces)
                                                    {
                                                        contentFilter.FilteredNamespaces[child->Index] = true;
                                                        if (recurse)
                                                            process(*child, process);
                                                    }
                                                    for (auto const& child : parent.Entries)
                                                    {
                                                        contentFilter.FilteredObjects[child->Index] = true;
                                                        if (recurse)
                                                            process(*child, process);
                                                    }
                                                },
                                                [recurse](ContentObject& parent, auto& process) -> void
                                                {
                                                    for (auto const& child : parent.Entries)
                                                    {
                                                        contentFilter.FilteredObjects[child->Index] = true;
                                                        if (recurse)
                                                            process(*child, process);
                                                    }
                                                },
                                            };
                                            process(ns, process);
                                        }
                                        if (scoped::ItemTooltip())
                                            I::TextUnformatted("Show all children\nHold Shift to recurse");
                                    }
                                }
                                I::TableNextColumn(); I::TextColored({ 1, 1, 1, 0.15f }, "Namespace");
                                I::TableNextColumn(); I::TextUnformatted("");
                                I::TableNextColumn(); I::TextColored({ 1, 1, 1, 0.15f }, DOMAINS[ns.Domain]); I::SetItemTooltip("Domain");
                                I::TableNextColumn(); I::TextUnformatted("");
                                I::TableNextColumn(); I::TextUnformatted("");
                                I::TableNextColumn(); I::TextUnformatted("");
                            }
                            else
                            {
                                auto const id = I::GetCurrentWindow()->GetID(&ns);
                                storeFocusedParentInfo(parentNamespaceIndex, id);
                                if ((open = I::TreeNodeUpdateNextOpen(id, 0) || collapseAll))
                                    I::TreePushOverrideID(id);
                            }

                            if (open)
                            {
                                auto pop = gsl::finally(&I::TreePop);

                                // Optimization: skip traversing the tree when all required items were drawn
                                if (clipper.ItemsCount && virtualIndex >= clipper.DisplayEnd)
                                    return;

                                // Optimization: don't traverse the tree when clipper is only measuring the first item's height
                                if (clipper.DisplayStart == 0 && clipper.DisplayEnd == 1)
                                    return;

                                for (auto const& child : ns.Namespaces)
                                {
                                    processNamespace(*child, namespaceIndex, processNamespace);

                                    // Optimization: stop traversing the tree early when all required items were drawn
                                    if (clipper.ItemsCount && virtualIndex >= clipper.DisplayEnd)
                                        return;
                                }
                                auto processEntries = [&](auto const& entries, auto& processEntries) -> void
                                {
                                    for (auto* child : entries)
                                    {
                                        ContentObject& entry = *child;
                                        if (!passesFilter(entry))
                                            continue;

                                        bool const hasEntries = !entry.Entries.empty();
                                        if (hasEntries)
                                        {
                                            if (expandAll) I::SetNextItemOpen(true);
                                            if (collapseAll) I::SetNextItemOpen(false);
                                        }

                                        bool open = false;
                                        if (auto const index = virtualIndex++; index >= clipper.DisplayStart && index < clipper.DisplayEnd)
                                        {
                                            auto const* currentViewer = dynamic_cast<ContentViewer*>(m_currentViewer);
                                            entry.Finalize();
                                            I::TableNextRow();
                                            I::TableNextColumn(); I::SetNextItemAllowOverlap(); open = I::TreeNodeEx(&entry, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding | (entry.Entries.empty() ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick) | (currentViewer && &currentViewer->Content == &entry ? ImGuiTreeNodeFlags_Selected : 0), "") && hasEntries;
                                            storeFocusedParentInfo(namespaceIndex);
                                            if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                                                OpenContent(entry, button & ImGuiButtonFlags_MouseButtonMiddle);
                                            if (scoped::PopupContextItem())
                                            {
                                                I::Text("Full Name: %s", to_utf8(entry.GetFullName()).c_str());
                                                if (I::InputTextUTF8("Name", g_config.ContentObjectNames, *entry.GetGUID(), entry.GetName() && entry.GetName()->Name && *entry.GetName()->Name ? *entry.GetName()->Name : entry.GetDisplayName()))
                                                    g_ui.ClearContentSortCache();

                                                DrawCopyButton("GUID", entry.GetGUID() ? *entry.GetGUID() : EmptyGUID, entry.GetGUID());
                                                I::SameLine();
                                                DrawCopyButton("UID", entry.GetUID() ? *entry.GetUID() : 0, entry.GetUID());
                                                I::SameLine();
                                                DrawCopyButton("Data ID", entry.GetDataID() ? *entry.GetDataID() : 0, entry.GetDataID());

                                                DrawCopyButton("Type Index", entry.Type->Index);
                                                I::SameLine();
                                                DrawCopyButton("Type Name", entry.Type->GetDisplayName());

                                                DrawCopyButton("Mangled Name", entry.GetName() ? *entry.GetName()->Name : L"", entry.GetName());
                                                I::SameLine();
                                                DrawCopyButton("Full Mangled Name", entry.GetFullName(), entry.GetName());

                                                DrawCopyButton("Name", entry.GetDisplayName(false, true));
                                                I::SameLine();
                                                DrawCopyButton("Full Name", entry.GetFullDisplayName(false, true));

                                                if (entry.Namespace && I::Button("Bruteforce Demangle Name"))
                                                    demangle.OpenBruteforceUI(std::format(L"{}.", entry.Namespace->GetFullDisplayName()), nullptr, true, true, false);

                                                if (I::Button("Search for Content References"))
                                                    contentSearch.SearchForSymbolValue("Content*", (TypeInfo::Condition::ValueType)entry.Data.data());
                                            }

                                            I::SameLine(0, I::GetStyle().FramePadding.x * 2); // TODO: Align paddings better
                                            if (open && contentFilter && !contentFilter.FilteredObjects.empty())
                                            {
                                                if (std::ranges::any_of(entry.Entries, [](auto const& child) { return !contentFilter.FilteredObjects[child->Index]; }))
                                                {
                                                    if (I::Button("<c=#4>" ICON_FA_FILTER_SLASH "</c>"))
                                                        for (auto const& child : entry.Entries)
                                                            contentFilter.FilteredObjects[child->Index] = true;
                                                    if (scoped::ItemTooltip())
                                                        I::TextUnformatted("Show all children");
                                                    I::SameLine();
                                                }
                                            }
                                            if (auto const icon = entry.GetIcon())
                                                if (DrawTexture(icon, { .Size = { 0, I::GetFrameHeight() } }))
                                                    I::SameLine();
                                            I::Text("%s", to_utf8(entry.GetDisplayName(m_showOriginalNames)).c_str());

                                            I::TableNextColumn(); I::TextUnformatted(to_utf8(entry.Type->GetDisplayName()).c_str());
                                            I::TableNextColumn(); I::Text("%u", entry.Data.size());
                                            I::TableNextColumn(); if (auto* id = entry.GetDataID()) I::Text("%i", *id);
                                            I::TableNextColumn(); if (auto* uid = entry.GetUID()) I::Text("%i", *uid);
                                            I::TableNextColumn(); if (auto* guid = entry.GetGUID()) { I::TextUnformatted(std::format("{}", *guid).c_str()); I::SetItemTooltip(std::format("{}", *guid).c_str()); }
                                            I::TableNextColumn(); if (!entry.IncomingReferences.empty()) I::TextColored({ 0, 1, 0, 1 }, ICON_FA_ARROW_LEFT "%u", (uint32)entry.IncomingReferences.size());
                                        }
                                        else if (hasEntries)
                                        {
                                            auto const id = I::GetCurrentWindow()->GetID(&entry);
                                            storeFocusedParentInfo(namespaceIndex, id);
                                            if ((open = (I::TreeNodeUpdateNextOpen(id, 0) || collapseAll) && hasEntries))
                                                I::TreePushOverrideID(id);
                                        }

                                        if (open)
                                        {
                                            auto pop = gsl::finally(&I::TreePop);

                                            // Optimization: skip traversing the tree when all required items were drawn
                                            if (clipper.ItemsCount && virtualIndex >= clipper.DisplayEnd)
                                                continue;

                                            // Optimization: don't traverse the tree when clipper is only measuring the first item's height
                                            if (clipper.DisplayStart == 0 && clipper.DisplayEnd == 1)
                                                continue;

                                            processEntries(getSortedContentObjects(false, entry.Index, entry.Entries), processEntries);
                                        }

                                        // Optimization: stop traversing the tree early when all required items were drawn
                                        if (clipper.ItemsCount && virtualIndex >= clipper.DisplayEnd)
                                            break;
                                    }
                                };
                                processEntries(getSortedContentObjects(true, ns.Index, ns.Entries), processEntries);
                            }
                        };

                        if (g_contentLoaded)
                        {
                            I::GetCurrentWindow()->SkipItems = false; // Workaround for bug with Expand/Collapse buttons not working if the last column is hidden
                            if (auto* root = g_contentRoot)
                                processNamespace(*root, 0, processNamespace); // Dry run to count elements
                            clipper.Begin(virtualIndex, I::GetFrameHeight());
                            if (navigateLeft() && focusedParentNamespaceIndex >= 0)
                                clipper.IncludeItemByIndex(focusedParentNamespaceIndex);
                            while (clipper.Step())
                            {
                                virtualIndex = 0;
                                if (auto* root = g_contentRoot)
                                    processNamespace(*root, 0, processNamespace);
                            }
                        }
                    }
                }
                if (scoped::TabItem(ICON_FA_COMMENT_CHECK " Conversations"))
                {
                    I::SetNextItemWidth(-(I::GetStyle().ItemSpacing.x + 60));
                    if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &m_conversationFilterString))
                        updateConversationSearch();
                    if (auto context = asyncConversationFilter.Current())
                    {
                        I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
                        if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
                        if (scoped::WithColorVar(ImGuiCol_Border, 0))
                        if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
                        if (scoped::WithColorVar(ImGuiCol_Text, 0))
                        if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                            if (context.IsIndeterminate())
                                I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                            else
                                I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
                    }
                    I::SameLine();
                    if (scoped::Disabled(!m_conversationFilterID))
                    {
                        I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
                        if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&m_conversationFilterRange, 0.1f, 0, 10000))
                            updateConversationSearch();
                        if (I::IsItemHovered())
                            I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    }

                    if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
                    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, I::GetStyle().ItemSpacing / 2))
                    if (scoped::Table("Table", 5, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable))
                    {
                        I::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 50, (ImGuiID)ConversationSort::GenID);
                        I::TableSetupColumn("~UID", ImGuiTableColumnFlags_WidthFixed, 50, (ImGuiID)ConversationSort::UID);
                        I::TableSetupColumn("Speaker Name", ImGuiTableColumnFlags_WidthStretch, 0, (ImGuiID)ConversationSort::StartingSpeakerName);
                        I::TableSetupColumn("Start State Text", ImGuiTableColumnFlags_WidthStretch, 0, (ImGuiID)ConversationSort::StartingStateText);
                        I::TableSetupColumn("Encountered", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80, (ImGuiID)ConversationSort::EncounteredTime);
                        I::TableSetupScrollFreeze(0, 1);
                        I::TableHeadersRow();

                        if (auto specs = I::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0)
                        {
                            m_conversationSort = (ConversationSort)specs->Specs[0].ColumnUserID;
                            m_conversationSortInvert = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                            specs->SpecsDirty = false;
                            updateConversationSort();
                        }

                        [&](auto _) -> void
                        {
                            std::scoped_lock __(conversationsLock);
                            ImGuiListClipper clipper;
                            clipper.Begin(filteredConversations.size());
                            while (clipper.Step())
                            {
                                for (auto conversationID : std::span(filteredConversations.begin() + clipper.DisplayStart, filteredConversations.begin() + clipper.DisplayEnd))
                                {
                                    scoped::WithID(conversationID);

                                    auto& conversation = conversations.at(conversationID);
                                    auto const* currentViewer = dynamic_cast<ConversationViewer*>(m_currentViewer);
                                    I::TableNextRow();

                                    I::TableNextColumn();
                                    I::Selectable(std::format("  <c=#4>{}</c>", conversationID).c_str(), currentViewer && currentViewer->ConversationID == conversationID ? ImGuiTreeNodeFlags_Selected : 0, ImGuiSelectableFlags_SpanAllColumns);

                                    I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, conversation.GetCompleteness() / (float)Conversation::COMPLETENESS_COMPLETE)));

                                    if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                                        OpenConversation(conversationID, button & ImGuiButtonFlags_MouseButtonMiddle);

                                    I::TableNextColumn();
                                    I::Text("<c=#8>%u</c>", conversation.UID);

                                    I::TableNextColumn();
                                    I::TextUnformatted(conversation.StartingSpeakerName().c_str());

                                    I::TableNextColumn();
                                    I::TextUnformatted(conversation.StartingStateText().c_str());

                                    I::TableNextColumn();
                                    if (conversation.EncounteredTime.time_since_epoch().count())
                                    {
                                        if (I::Selectable(std::format("<c=#{}>{}</c> {}###EncounteredTime", conversation.Map ? "F" : "2", ICON_FA_GLOBE, format_duration_colored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - conversation.EncounteredTime))).c_str()))
                                        {
                                            // TODO: Open map to { conversation.Map, conversation.Position }
                                        }
                                        if (scoped::ItemTooltip())
                                            I::TextUnformatted(std::format("Encountered on: {:%F %T}", std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(conversation.EncounteredTime))).c_str());
                                    }
                                }
                            }
                        }(0);
                    }
                }
                if (scoped::TabItem(ICON_FA_SEAL " Events"))
                {
                    I::SetNextItemWidth(-(I::GetStyle().ItemSpacing.x + 60));
                    if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &m_eventFilterString))
                        updateEventSearch();
                    if (auto context = asyncEventFilter.Current())
                    {
                        I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
                        if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
                        if (scoped::WithColorVar(ImGuiCol_Border, 0))
                        if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
                        if (scoped::WithColorVar(ImGuiCol_Text, 0))
                        if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                            if (context.IsIndeterminate())
                                I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                            else
                                I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
                    }
                    I::SameLine();
                    if (scoped::Disabled(!m_eventFilterID))
                    {
                        I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
                        if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&m_eventFilterRange, 0.1f, 0, 10000))
                            updateEventSearch();
                        if (I::IsItemHovered())
                            I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    }

                    auto filter = [&, next = false](std::string_view text, bool& filter) mutable
                    {
                        if (std::exchange(next, true))
                            I::SameLine();
                        if (I::Button(std::format("<c=#{}><c=#8>{}</c> {}</c>###EventFilter{}", filter ? "F" : "4", ICON_FA_FILTER, text, text).c_str()))
                        {
                            filter ^= true;
                            updateEventFilter();
                        }
                    };
                    filter("Normal", m_eventFilters.Normal);
                    filter("Group", m_eventFilters.Group);
                    filter("Meta", m_eventFilters.Meta);
                    filter("Dungeon", m_eventFilters.Dungeon);
                    filter("Non-Event", m_eventFilters.NonEvent);

                    if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
                    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, I::GetStyle().ItemSpacing / 2))
                    if (scoped::Table("Table", 5, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable))
                    {
                        I::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50, (ImGuiID)EventSort::ID);
                        I::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch, 0, (ImGuiID)EventSort::Map);
                        I::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 50, (ImGuiID)EventSort::Type);
                        I::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 0, (ImGuiID)EventSort::Title);
                        I::TableSetupColumn("Encountered", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80, (ImGuiID)EventSort::EncounteredTime);
                        I::TableSetupScrollFreeze(0, 1);
                        I::TableHeadersRow();

                        if (auto specs = I::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0)
                        {
                            m_eventSort = (EventSort)specs->Specs[0].ColumnUserID;
                            m_eventSortInvert = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                            specs->SpecsDirty = false;
                            updateEventSort();
                        }

                        [&](auto _) -> void
                        {
                            std::scoped_lock __(eventsLock);
                            ImGuiListClipper clipper;
                            clipper.Begin(filteredEvents.size());
                            while (clipper.Step())
                            {
                                for (auto eventID : std::span(filteredEvents.begin() + clipper.DisplayStart, filteredEvents.begin() + clipper.DisplayEnd))
                                {
                                    scoped::WithID(eventID.Map << 17 | eventID.UID);

                                    auto& event = events.at(eventID);
                                    auto const* currentViewer = dynamic_cast<EventViewer*>(m_currentViewer);
                                    I::TableNextRow();

                                    I::TableNextColumn();
                                    I::Selectable(std::format("{}", eventID.UID).c_str(), currentViewer && currentViewer->EventID == eventID ? ImGuiTreeNodeFlags_Selected : 0, ImGuiSelectableFlags_SpanAllColumns);
                                    if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                                        OpenEvent(eventID, button & ImGuiButtonFlags_MouseButtonMiddle);

                                    I::TableNextColumn();
                                    I::TextUnformatted(to_utf8(event.Map()).c_str());

                                    I::TableNextColumn();
                                    I::TextUnformatted(event.Type().c_str());

                                    I::TableNextColumn();
                                    I::TextUnformatted(to_utf8(event.Title()).c_str());

                                    I::TableNextColumn();
                                    if (auto time = event.EncounteredTime(); time.time_since_epoch().count())
                                    {
                                        if (I::Selectable(std::format("<c=#{}>{}</c> {}###EncounteredTime", eventID.Map ? "F" : "2", ICON_FA_GLOBE, format_duration_colored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - time))).c_str()))
                                        {
                                            // TODO: Open map to { eventID.Map, ?position? }
                                        }
                                        if (scoped::ItemTooltip())
                                            I::TextUnformatted(std::format("Encountered on: {:%F %T}", std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(time))).c_str());
                                    }
                                }
                            }
                        }(0);
                    }
                }
                if (scoped::TabItem(ICON_FA_BOOKMARK " Bookmarks"))
                {
                    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, { I::GetStyle().FramePadding.x, 0 }))
                    if (scoped::Table("Table", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable))
                    {
                        I::TableSetupColumn("Bookmark", ImGuiTableColumnFlags_WidthStretch);
                        I::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
                        I::TableSetupScrollFreeze(0, 1);
                        I::TableHeadersRow();

                        for (auto const& bookmark : g_config.BookmarkedContentObjects)
                        {
                            I::TableNextRow();
                            I::TableNextColumn(); DrawContentButton(GetContentObjectByGUID(bookmark.Value), &bookmark, { .MissingContentName = "CONTENT OBJECT MISSING" });
                            I::TableNextColumn(); I::TextUnformatted(format_duration_colored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - bookmark.Time)).c_str());
                        }
                    }
                }
                if (scoped::TabItem(ICON_FA_WRENCH " Tools"))
                {
                    if (scoped::WithStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0, I::GetStyle().ButtonTextAlign.y }))
                    {
                        if (I::Button(ICON_FA_GLOBE " World Map", { -FLT_MIN, 0 }); auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                            OpenWorldMap(button & ImGuiButtonFlags_MouseButtonMiddle);
                    }
                }
            }
        }
        if (I::SameLine(); scoped::Child("ViewerPane", { }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Border))
        {
            if (scoped::TabBar("Tabs", ImGuiTabBarFlags_TabListPopupButton/* | ImGuiTabBarFlags_AutoSelectNewTabs*/ | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_Reorderable))
            {
                auto tabBar = I::GetCurrentTabBar();
                int selectedTabOrder = -1;
                std::unique_ptr<Viewer> const* toRemove = nullptr;
                for (auto& viewer : m_viewers)
                {
                    bool open = true;
                    if (scoped::TabItem(std::format("{}###Viewer-{}", viewer->Title(), viewer->ID).c_str(), &open, viewer->SetSelected ? ImGuiTabItemFlags_SetSelected : 0))
                    {
                        if (open)
                            m_currentViewer = viewer.get();

                        viewer->Draw();
                    }
                    if (auto tab = I::TabBarGetCurrentTab(tabBar))
                    {
                        if (tab->ID == tabBar->SelectedTabId)
                            selectedTabOrder = I::TabBarGetTabOrder(tabBar, tab);
                        if (viewer->SetAfterCurrent)
                            if (int offset = selectedTabOrder - I::TabBarGetTabOrder(tabBar, tab) + 1)
                                I::TabBarQueueReorder(I::GetCurrentTabBar(), tab, offset);
                    }
                    viewer->SetSelected = false;
                    viewer->SetAfterCurrent = false;
                    if (!open)
                        toRemove = &viewer;
                }
                if (toRemove)
                {
                    if (m_currentViewer == toRemove->get())
                        m_currentViewer = nullptr;
                    m_viewers.erase(std::ranges::find(m_viewers, *toRemove));
                }
            }
        }
    }

    UploadLoadedTexturesToGPU();
    static bool firstTime = [&]
    {
        if (!g_config.GameDatPath.empty())
            g_archives.Add(ArchiveKind::Game, g_config.GameDatPath);
        if (!g_config.LocalDatPath.empty())
            g_archives.Add(ArchiveKind::Local, g_config.LocalDatPath);
        m_progress[0].Run([this, updateFileFilter, updateStringSort, updateStringSearch, updateStringFilter, updateContentFilter, updateConversationSearch, updateEventFilter](ProgressBarContext& progress)
        {
            progress.Start("Preparing decryption key storage");
            if (!g_config.DecryptionKeysPath.empty())
            {
                auto const extension = std::wstring(std::from_range, std::filesystem::path(g_config.DecryptionKeysPath).extension().wstring() | std::views::transform(towlower));
                if (extension == L".sqlite")
                {
                    using namespace sqlite;
                    std::filesystem::path const path = g_config.DecryptionKeysPath;
                    progress.Start("Reading string decryption keys");
                    static database db(path.u16string(), { .flags = OpenFlags::READONLY });
                    static std::unique_ptr<DBLoadingOperation> operations[]
                    {
                        DBLoadingOperation::Make("Texts",
                            "TextID, Key, Time, Session, Map, ClientX, ClientY, ClientZ, ClientFacing",
                            [](uint32 stringID, uint64 key, uint32 time, uint32 session, uint32 map, float x, float y, float z, float facing) 
                            {
                                AddDecryptionKeyInfo(stringID, { key, time, session, map, { x, y, z, facing } });
                            },
                            {
                                .Condition = "Key",
                                .SharedMutex = &decryptionKeysLock,
                                .PostHandler = [=]
                                {
                                    if (!m_stringFilterString.empty() && !m_stringFilterID)
                                        updateStringSearch();
                                    else if (m_stringSort == StringSort::Text || m_stringSort == StringSort::DecryptionTime)
                                        updateStringSort();
                                },
                            }
                        ),
                        DBLoadingOperation::Make("Assets",
                            "AssetType, AssetID, Key",
                            [](uint32 assetType, uint32 assetID, uint64 key)
                            {
                                AddDecryptionKey((EncryptedAssetType)assetType, assetID, key);
                                if ((EncryptedAssetType)assetType == EncryptedAssetType::Voice)
                                    WipeVoiceCache(assetID);
                            },
                            {
                                .Condition = "Key",
                                .SharedMutex = &decryptionKeysLock,
                            }
                        ),
                        DBLoadingOperation::Make("Conversations",
                            "GenID, UID, FirstEncounteredTime, LastEncounteredTime",
                            [](uint32 GenID, uint32 UID, uint32 FirstEncounteredTime, uint32 LastEncounteredTime)
                            {
                                conversations[GenID].UID = UID;
                            },
                            {
                                .SharedMutex = &conversationsLock,
                                .PostHandler = updateConversationSearch,
                            }
                        ),
                        DBLoadingOperation::Make("ConversationStates",
                            "GenID, StateID, TextID, SpeakerNameTextID, SpeakerPortraitOverrideFileID, Priority, Flags, Voting, Timeout, CostAmount, CostType, Unk",
                            [](uint32 GenID, uint32 StateID, uint32 TextID, uint32 SpeakerNameTextID, uint32 SpeakerPortraitOverrideFileID, uint32 Priority, uint32 Flags, uint32 Voting, uint32 Timeout, uint32 CostAmount, uint32 CostType, uint32 Unk)
                            {
                                conversations[GenID].States.emplace(StateID, TextID, SpeakerNameTextID, SpeakerPortraitOverrideFileID, Priority, Flags, Voting, Timeout, CostAmount, CostType, Unk);
                            },
                            {
                                .SharedMutex = &conversationsLock,
                                .PostHandler = updateConversationSearch,
                            }
                        ),
                        DBLoadingOperation::Make("ConversationStateTransitions",
                            "GenID, StateID, StateTextID, TransitionID, TextID, CostAmount, CostType, CostKarma, Diplomacy, Unk, Personality, Icon, SkillDefDataID",
                            [](uint32 GenID, uint32 StateID, uint32 StateTextID, uint32 TransitionID, uint32 TextID, uint32 CostAmount, uint32 CostType, uint32 CostKarma, uint32 Diplomacy, uint32 Unk, uint32 Personality, uint32 Icon, uint32 SkillDefDataID)
                            {
                                for (auto& state : conversations[GenID].States | std::views::filter([StateID, StateTextID](auto const& state) { return state.StateID == StateID && state.TextID == StateTextID; }))
                                    state.Transitions.emplace(TransitionID, TextID, CostAmount, CostType, CostKarma, Diplomacy, Unk, Personality, Icon, SkillDefDataID);
                            },
                            {
                                .SharedMutex = &conversationsLock,
                                .PostHandler = updateConversationSearch,
                            }
                        ),
                        DBLoadingOperation::Make("ConversationStateTransitionTargets",
                            "GenID, StateID, StateTextID, TransitionID, TransitionTextID, TargetStateID, Flags",
                            [](uint32 GenID, uint32 StateID, uint32 StateTextID, uint32 TransitionID, uint32 TransitionTextID, uint32 TargetStateID, uint32 Flags)
                            {
                                for (auto& state : conversations[GenID].States | std::views::filter([StateID, StateTextID](auto const& state) { return state.StateID == StateID && state.TextID == StateTextID; }))
                                    for (auto& transition : state.Transitions | std::views::filter([TransitionID, TransitionTextID](auto const& transition) { return transition.TransitionID == TransitionID && transition.TextID == TransitionTextID; }))
                                        transition.Targets.emplace(TargetStateID, Flags);
                            },
                            {
                                .SharedMutex = &conversationsLock,
                                .PostHandler = updateConversationSearch,
                            }
                        ),
                        DBLoadingOperation::Make("AgentConversation",
                            "ConversationGenID, ConversationStateID, ConversationStateTextID, ConversationStateTransitionID, ConversationStateTransitionTextID, Time, Session, Map, AgentX, AgentY, AgentZ, AgentFacing",
                            [](uint32 ConversationGenID, uint32 ConversationStateID, uint32 ConversationStateTextID, uint32 ConversationStateTransitionID, uint32 ConversationStateTransitionTextID, uint64 Time, uint32 Session, uint32 Map, float AgentX, float AgentY, float AgentZ, float AgentFacing)
                            {
                                auto& conversation = conversations[ConversationGenID];
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
                                .SharedMutex = &conversationsLock,
                                .PostHandler = updateConversationSearch,
                            }
                        ),
                        DBLoadingOperation::Make("Events",
                            "Map, UID, TitleTextID, TitleParameterTextID1, TitleParameterTextID2, TitleParameterTextID3, TitleParameterTextID4, TitleParameterTextID5, TitleParameterTextID6, DescriptionTextID, FileIconID, FlagsClient, FlagsServer, Level, MetaTextTextID, AudioEffect, A, Time",
                            [](uint32 Map, uint32 UID, uint32 TitleTextID, uint32 TitleParameterTextID1, uint32 TitleParameterTextID2, uint32 TitleParameterTextID3, uint32 TitleParameterTextID4, uint32 TitleParameterTextID5, uint32 TitleParameterTextID6, uint32 DescriptionTextID, uint32 FileIconID, uint32 FlagsClient, uint32 FlagsServer, uint32 Level, uint32 MetaTextTextID, std::vector<byte> const& AudioEffect, uint32 A, uint64 Time)
                            {
                                events[{ Map, UID }].States.emplace(Map, UID, TitleTextID, std::array { TitleParameterTextID1, TitleParameterTextID2, TitleParameterTextID3, TitleParameterTextID4, TitleParameterTextID5, TitleParameterTextID6 }, DescriptionTextID, FileIconID, (Event::State::ClientFlags)FlagsClient, (Event::State::ServerFlags)FlagsServer, Level, MetaTextTextID, AudioEffect, A, Time).first->Time = Time;
                            },
                            {
                                .SharedMutex = &eventsLock,
                                .PostHandler = updateEventFilter,
                            }
                        ),
                        DBLoadingOperation::Make("Objectives",
                            "Map, EventUID, EventObjectiveIndex, Type, Flags, TargetCount, TextID, AgentNameTextID, ProgressBarStyle, ExtraInt, ExtraInt2, ExtraGUID, ExtraGUID2, ExtraBlob, Time",
                            [](uint32 Map, uint32 EventUID, uint32 EventObjectiveIndex, uint32 Type, uint32 Flags, uint32 TargetCount, uint32 TextID, uint32 AgentNameTextID, std::vector<byte> const& ProgressBarStyle, uint32 ExtraInt, uint32 ExtraInt2, std::vector<byte> const& ExtraGUID, std::vector<byte> const& ExtraGUID2, std::vector<byte> const& ExtraBlob, uint64 Time)
                            {
                                events[{ Map, EventUID }].Objectives.emplace(Map, EventUID, EventObjectiveIndex, Type, Flags, TargetCount, TextID, AgentNameTextID, ProgressBarStyle, ExtraInt, ExtraInt2, ExtraGUID, ExtraGUID2, ExtraBlob, Time).first->Time = Time;
                            },
                            {
                                .SharedMutex = &eventsLock,
                                .PostHandler = updateEventFilter,
                            }
                        ),
                        DBLoadingOperation::Make("ObjectiveAgents",
                            "ObjectiveMap, ObjectiveEventUID, ObjectiveEventObjectiveIndex, ObjectiveAgentIndex, ObjectiveAgentID, IFNULL(NULLIF(ObjectiveAgentNameTextID, 0), NameTextID), ObjectiveAgentX, ObjectiveAgentY, ObjectiveAgentZ, ObjectiveAgentFacing",
                            [](uint32 ObjectiveMap, uint32 ObjectiveEventUID, uint32 ObjectiveEventObjectiveIndex, uint32 ObjectiveAgentIndex, uint32 ObjectiveAgentID, uint32 ObjectiveAgentNameTextID, float ObjectiveAgentX, float ObjectiveAgentY, float ObjectiveAgentZ, float ObjectiveAgentFacing)
                            {
                                for (auto& objective : events[{ ObjectiveMap, ObjectiveEventUID }].Objectives | std::views::filter([ObjectiveEventObjectiveIndex](auto const& objective) { return objective.EventObjectiveIndex == ObjectiveEventObjectiveIndex; }))
                                {
                                    objective.Agents.resize(std::max<size_t>(objective.Agents.size(), ObjectiveAgentIndex + 1));
                                    objective.Agents.at(ObjectiveAgentIndex) = { ObjectiveAgentID, ObjectiveAgentNameTextID };
                                }
                            },
                            {
                                .Joins = "LEFT JOIN Agents a ON ObjectiveAgents.Session=a.Session AND ObjectiveAgents.MapSession=a.MapSession AND ObjectiveAgents.ObjectiveAgentID=a.AgentID",
                                .SharedMutex = &eventsLock,
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
                if (extension == L".txt")
                {
                    std::filesystem::path const path = LR"(E:\Program Files\Guild Wars 2\addons\arcdps\arcdps_chatlog_keys.txt)";
                    progress.Start("Reading string decryption keys", file_size(path));
                    std::ifstream file(path);
                    std::string buffer;
                    uint32 session = 0;
                    std::chrono::local_seconds time;
                    while (std::getline(file, buffer))
                    {
                        uint32 stringID;
                        uint64 key;
                        if (std::string timeString; scn::scan(buffer, "; Session: {:[^\r\n]}", timeString))
                        {
                            std::istringstream(timeString) >> std::chrono::parse("%F %T", time);
                            ++session;
                        }
                        if (scn::scan(buffer, "{} = {:x}", stringID, key))
                            AddDecryptionKeyInfo(stringID, { .Key = key, .Time = std::chrono::system_clock::to_time_t(std::chrono::current_zone()->to_sys(time)), .Session = session });

                        progress = file.tellg();
                    }
                }
                if (extension == L".txt")
                {
                    std::filesystem::path const path = LR"(E:\Program Files\Guild Wars 2\addons\arcdps\arcdps_chatlog_asset_keys.txt)";
                    progress.Start("Reading asset decryption keys", file_size(path));
                    std::ifstream file(path);
                    std::string buffer;
                    while (std::getline(file, buffer))
                    {
                        uint32 assetType, assetID;
                        uint64 key;
                        if (scn::scan(buffer, "{} {} = {:x}", assetType, assetID, key))
                            AddDecryptionKey((EncryptedAssetType)assetType, assetID, key);

                        progress = file.tellg();
                    }
                }
            }

            g_archives.Load(progress);
            auto archivePtr = g_archives.GetArchive();
            if (!archivePtr)
                return;
            auto& archive = *archivePtr;

            progress.Start("Creating file list");
            updateFileFilter();

            // Wait for PackFile layouts to load before continuing
            while (!pf::Layout::g_loaded)
                std::this_thread::sleep_for(50ms);

            // Can't parallelize currently, Archive supports only single-thread file loading

            LoadTextPackManifest(archive, progress);
            LoadTextPackVariants(archive, progress);
            LoadTextPackVoices(archive, progress);
            LoadStringsFiles(archive, progress);
            progress.Start("Creating string list");
            updateStringFilter();
            progress.Start("Creating conversation list");
            updateConversationSearch();
            progress.Start("Creating event list");
            updateEventFilter();

            LoadBankIndexData(archive, progress);
            LoadContentFiles(archive, progress);
            m_progress[2].Run([=](ProgressBarContext& progress)
            {
                ProcessContentFiles(progress);
                updateContentFilter();

                progress.Start("Processing content types for migration");
                if (!g_config.LastNumContentTypes)
                    g_config.LastNumContentTypes = g_contentTypeInfos.size();
                if (g_config.LastNumContentTypes == g_contentTypeInfos.size())
                {
                    for (auto const& type : g_contentTypeInfos)
                    {
                        auto const itr = g_config.TypeInfo.find(type->Index);
                        if (itr == g_config.TypeInfo.end())
                            continue;

                        TypeInfo& typeInfo = itr->second;
                        if (typeInfo.Examples.empty() && !type->Objects.empty() && type->GUIDOffset >= 0)
                            typeInfo.Examples.insert_range(type->Objects | std::views::take(5) | std::views::transform([](ContentObject const* content) { return *content->GetGUID(); }));
                    }
                }
                else
                    windows.MigrateContentTypes = true;
            });
        });
        m_progress[1].Run([](ProgressBarContext& progress)
        {
            progress.Start("Parsing PackFile layouts from Gw2-64.exe");
            if (!g_config.GameExePath.empty())
                pf::Layout::ParsePackFileLayout(g_config.GameExePath);
        });
        return true;
    }();
}

std::string UI::MakeDataLink(byte type, uint32 id)
{
    switch (type)
    {
        case 2:
        {
#pragma pack(push, 1)
            struct
            {
                byte Type;
                byte Count;
                uint32 ID;
                byte Payload[4 + 4 + 4 + 8 + 8];
            } dataLink { type, 1, id };
#pragma pack(pop)
            static_assert(sizeof(dataLink) == 6 + 28);

            enum : uint32
            {
                HAS_SKIN            = 0x80000000,
                HAS_UPGRADE_1       = 0x40000000,
                HAS_UPGRADE_2       = 0x20000000,
                HAS_KEY_NAME        = 0x10000000,
                HAS_KEY_DESCRIPTION = 0x08000000,
            };

            uint32 payloadSize = 0;
            if (ContentObject* item = GetContentObjectByDataID(Content::ItemDef, id))
            {
                if (auto const key = GetDecryptionKey((*item)["Name"]); key && *key)
                {
                    dataLink.ID |= HAS_KEY_NAME;
                    *(uint64*)&dataLink.Payload[payloadSize] = *key;
                    payloadSize += sizeof(uint64);
                }
                if (auto const key = GetDecryptionKey((*item)["Description"]); key && *key)
                {
                    dataLink.ID |= HAS_KEY_DESCRIPTION;
                    *(uint64*)&dataLink.Payload[payloadSize] = *key;
                    payloadSize += sizeof(uint64);
                }
            }
            return std::format("[&{}]", base64_encode(std::string_view((char const*)&dataLink, offsetof(decltype(dataLink), Payload) + payloadSize)));
            break;
        }
        default:
        {
#pragma pack(push, 1)
            struct
            {
                byte Type;
                uint32 ID;
            } dataLink { type, id };
#pragma pack(pop)
            static_assert(sizeof(dataLink) == 5);
            return std::format("[&{}]", base64_encode(std::string_view((char const*)&dataLink, sizeof(dataLink))));
            break;
        }
    }
}

void UI::PlayVoice(uint32 voiceID)
{
    auto const data = GetVoice(voiceID, g_config.Language);
    if (data.empty())
        return;

    static std::unique_ptr<FMOD::System, decltype([](FMOD::System* system) { system->release(); })> system([]() -> FMOD::System*
    {
        FMOD::System* system;
        if (System_Create(&system) != FMOD_OK)
            return nullptr;
        if (system->init(32, FMOD_INIT_NORMAL, nullptr) != FMOD_OK)
            return nullptr;
        return system;
    }());

    if (!system)
        return;

    FMOD_CREATESOUNDEXINFO info
    {
        .cbsize = sizeof(FMOD_CREATESOUNDEXINFO),
        .length = (uint32)data.size(),
    };
    try
    {
        static FMOD::Channel* channel = nullptr;
        if (channel)
            std::exchange(channel, nullptr)->stop();

        FMOD::Sound* sound;
        if (system->createSound((char const*)data.data(), FMOD_OPENMEMORY, &info, &sound) != FMOD_OK)
        {
            auto const key = GetDecryptionKey(EncryptedAssetType::Voice, voiceID);
            if (!key)
                return;

            std::vector encrypted { std::from_range, data };
            RC4(RC4::MakeKey(*key)).Crypt(encrypted);
            if (system->createSound((char const*)encrypted.data(), FMOD_OPENMEMORY, &info, &sound) != FMOD_OK)
                return;

            if (I::GetIO().KeyAlt)
                return ExportData(encrypted, std::format(R"(Export\Voice\English\{}.mp3)", voiceID));
        }

        if (I::GetIO().KeyAlt)
            return ExportData(data, std::format(R"(Export\Voice\English\{}.mp3)", voiceID));

        system->playSound(sound, nullptr, false, &channel);
    }
    catch(...) { }
}

void UI::ExportData(std::span<byte const> data, std::filesystem::path const& path)
{
    create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write((char const*)data.data(), data.size());
}

#pragma region Viewers

struct UI::RawFileViewer : Viewer
{
    ArchiveFile File;
    std::stack<ArchiveFile> HistoryPrev;
    std::stack<ArchiveFile> HistoryNext;
    std::vector<byte> RawData;

    RawFileViewer(uint32 id, bool newTab, ArchiveFile const& file) : Viewer(id, newTab), File(file), RawData(File.Source.get().Archive.GetFile(file.ID)) { }

    virtual void Initialize() { }

    std::string Title() override
    {
        if (&File.Source.get().Archive != g_archives.GetArchive())
            return std::format("<c=#4>File #</c>{}<c=#4> ({})</c>", File.ID, File.Source.get().Path.filename().string());
        return std::format("<c=#4>File #</c>{}", File.ID);
    }
    void Draw() override;
    virtual void DrawOutline() { }
    virtual void DrawPreview() { DrawTexture(File.ID, { .Data = &RawData }); }
};
void UI::RawFileViewer::Draw()
{
    auto _ = scoped_seh_exception_handler::Create();

    bool drawHex = false;
    bool drawOutline = false;
    bool drawPreview = false;
    if (static ImGuiID sharedScope = 2; scoped::Child(sharedScope, { }, ImGuiChildFlags_Border | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
    {
        if (scoped::Disabled(HistoryPrev.empty()); I::Button(ICON_FA_ARROW_LEFT "##HistoryBack") || I::IsEnabled() && I::GetIO().MouseClicked[3])
        {
            auto const file = HistoryPrev.top();
            HistoryPrev.pop();
            HistoryNext.emplace(File);
            g_ui.OpenFile(file, false, true);
        }
        I::SameLine(0, 0);
        if (scoped::Disabled(HistoryNext.empty()); I::Button(ICON_FA_ARROW_RIGHT "##HistoryNext") || I::IsEnabled() && I::GetIO().MouseClicked[4])
        {
            auto const file = HistoryNext.top();
            HistoryNext.pop();
            HistoryPrev.emplace(File);
            g_ui.OpenFile(file, false, true);
        }
        I::SameLine();
        if (scoped::TabBar("Tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_NoTabListScrollingButtons))
        {
            if (scoped::TabItem(ICON_FA_INFO " Info", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
            if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
            {
                drawHex = true;
            }
            if (scoped::TabItem(ICON_FA_BINARY " Data", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                drawHex = true;
            if (scoped::TabItem(ICON_FA_FOLDER_TREE " Outline", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                drawOutline = true;
            if (scoped::TabItem(ICON_FA_IMAGE " Preview", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                drawPreview = true;
        }
    }

    if (drawOutline)
        if (scoped::Child("Outline"))
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
            DrawOutline();
    if (drawPreview)
        DrawPreview();

    if (!drawHex)
        return;

    static std::optional<uint32> highlightOffset;
    HexViewerOptions options
    {
        .ShowHeaderRow = true,
        .ShowOffsetColumn = true,
        .FillWindow = true,
        .OutHighlightOffset = highlightOffset,
    };
    DrawHexViewer(RawData, options);
    if (auto const offset = options.OutHighlightOffset)
        highlightOffset = offset;
    else if (options.OutHoveredInfo)
        highlightOffset.reset();
    if (options.OutHoveredInfo)
    {
        auto const& [byteOffset, cellCursor, cellSize, tableCursor, tableSize] = *options.OutHoveredInfo;
        I::GetWindowDrawList()->AddRectFilled(ImVec2(tableCursor.x, cellCursor.y + 2), ImVec2(tableCursor.x + tableSize.x, cellCursor.y + cellSize.y - 2), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.2f }));
        I::GetWindowDrawList()->AddRectFilled(ImVec2(cellCursor.x + 2, tableCursor.y), ImVec2(cellCursor.x + cellSize.x - 2, tableCursor.y + tableSize.y), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.2f }));
    }
}
bool UI::IsViewerRawFile(Viewer const* viewer, ArchiveFile const& file)
{
    auto const* currentViewer = dynamic_cast<RawFileViewer const*>(viewer);
    return currentViewer && currentViewer->File == file;
}

struct UI::ContentViewer : Viewer
{
    ContentObject& Content;
    std::stack<ContentObject*> HistoryPrev;
    std::stack<ContentObject*> HistoryNext;

    ContentViewer(uint32 id, bool newTab, ContentObject& content): Viewer(id, newTab), Content(content)
    {
        content.Finalize();
    }

    std::string Title() override { return to_utf8(std::format(L"<c=#4>{}</c> {}", Content.Type->GetDisplayName(), Content.GetDisplayName())); }
    void Draw() override;
};
void UI::ContentViewer::Draw()
{
    auto _ = scoped_seh_exception_handler::Create();

    auto& typeInfo = g_config.TypeInfo.try_emplace(Content.Type->Index).first->second;
    typeInfo.Initialize(*Content.Type);

    auto tabScopeID = I::GetCurrentWindow()->IDStack.back();
    if (static ImGuiID sharedScope = 1; scoped::Child(sharedScope, { }, ImGuiChildFlags_Border | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
    {
        if (scoped::Disabled(HistoryPrev.empty()); I::Button(ICON_FA_ARROW_LEFT "##HistoryBack") || I::IsEnabled() && I::GetIO().MouseClicked[3])
        {
            auto* content = HistoryPrev.top();
            HistoryPrev.pop();
            HistoryNext.emplace(&Content);
            g_ui.OpenContent(*content, false, true);
        }
        I::SameLine(0, 0);
        if (scoped::Disabled(HistoryNext.empty()); I::Button(ICON_FA_ARROW_RIGHT "##HistoryNext") || I::IsEnabled() && I::GetIO().MouseClicked[4])
        {
            auto* content = HistoryNext.top();
            HistoryNext.pop();
            HistoryPrev.emplace(&Content);
            g_ui.OpenContent(*content, false, true);
        }

        I::SameLine();
        if (auto const* guid = Content.GetGUID())
            if (auto bookmarked = std::ranges::contains(g_config.BookmarkedContentObjects, *guid, &Config::Bookmark<GUID>::Value); I::CheckboxButton(ICON_FA_BOOKMARK, bookmarked, "Bookmark", I::GetFrameHeight()))
                if (bookmarked)
                    g_config.BookmarkedContentObjects.emplace(std::chrono::system_clock::now(), *guid);
                else
                    std::erase_if(g_config.BookmarkedContentObjects, [guid = *guid](auto const& record) { return record.Value == guid; });

        I::SameLine();
        if (I::Button(std::format("<c=#{}>{}</c> <c=#{}>{}</c>", g_config.TreeContentStructLayout ? "4" : "F", ICON_FA_LIST, g_config.TreeContentStructLayout ? "F" : "4", ICON_FA_FOLDER_TREE).c_str()))
            g_config.TreeContentStructLayout ^= true;

        I::SameLine();
        if (scoped::TabBar("Tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_NoTabListScrollingButtons))
        {
            if (scoped::TabItem(ICON_FA_INFO " Info", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
            if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
            if (scoped::OverrideID(tabScopeID)) // Inherit tab's ID scope, to ensure that keyboard focus is cleared when tabs change
            {
                auto const dataID = Content.GetDataID();
                I::SetNextItemWidth(100);
                I::InputTextReadOnly("##FullMangledName", to_utf8(Content.GetFullName()).c_str());
                I::SameLine();
                I::SetNextItemWidth(dataID ? -90 : -FLT_MIN);
                I::InputTextReadOnly("##FullName", to_utf8(Content.GetFullDisplayName()));
                if (dataID)
                {
                    I::SameLine();
                    I::SetNextItemWidth(20);
                    I::DragScalar("##DataLinkType", ImGuiDataType_U8, &typeInfo.DataLinkType, 0.1f, nullptr, nullptr, "%02hhX");
                    I::SameLine();
                    I::SetNextItemWidth(-FLT_MIN);
                    if (scoped::Disabled(!typeInfo.DataLinkType))
                    if (I::Button(ICON_FA_COPY " DataLink"))
                        I::SetClipboardText(g_ui.MakeDataLink(typeInfo.DataLinkType, *dataID).c_str());
                }
                if (scoped::Group())
                {
                    if (I::InputTextUTF8("Content Name", g_config.ContentObjectNames, *Content.GetGUID(), Content.GetName() && Content.GetName()->Name && *Content.GetName()->Name ? *Content.GetName()->Name : Content.GetDisplayName()))
                        g_ui.ClearContentSortCache();
                    I::InputTextUTF8("Namespace Name", g_config.ContentNamespaceNames, Content.Namespace->GetFullName(), Content.Namespace->Name);
                    I::InputTextWithHint("Type Name", to_utf8(Content.Type->GetDisplayName()).c_str(), &typeInfo.Name);
                }
                I::SameLine();
                I::AlignTextToFramePadding();
                I::TextUnformatted(ICON_FA_PEN_TO_SQUARE);
                if (scoped::ItemTooltip())
                    I::TextUnformatted("Type Notes");
                I::SameLine();
                if (scoped::Group())
                    I::InputTextMultiline("##TypeNotes", &typeInfo.Notes, { -1, -1 }, ImGuiInputTextFlags_AllowTabInput);
            }

            if (!Content.OutgoingReferences.empty())
                I::PushStyleColor(ImGuiCol_Text, 0xFF00FF00);
            if (scoped::TabItem(std::format(ICON_FA_ARROW_RIGHT " Outgoing References ({})###OutgoingReferences", Content.OutgoingReferences.size()).c_str(), nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton | (Content.OutgoingReferences.empty() ? 0 : ImGuiTabItemFlags_UnsavedDocument)))
            {
                using enum ContentObject::Reference::Types;
                if (!Content.OutgoingReferences.empty())
                    I::PopStyleColor();
                I::SetNextWindowSizeConstraints({ }, { FLT_MAX, 300 });
                if (scoped::Child("Scroll", { }, ImGuiChildFlags_AutoResizeY))
                if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
                for (auto const& [object, type] : Content.OutgoingReferences)
                    DrawContentButton(object, &object, { .Icon = type == Root ? ICON_FA_ARROW_TURN_DOWN_RIGHT : type == Tracked ? ICON_FA_CHEVRONS_RIGHT : ICON_FA_ARROW_RIGHT });
            }
            else if (!Content.OutgoingReferences.empty())
                I::PopStyleColor();

            if (!Content.IncomingReferences.empty())
                I::PushStyleColor(ImGuiCol_Text, 0xFF00FF00);
            if (scoped::TabItem(std::format(ICON_FA_ARROW_LEFT " Incoming References ({})###IncomingReferences", Content.IncomingReferences.size()).c_str(), nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton | (Content.IncomingReferences.empty() ? 0 : ImGuiTabItemFlags_UnsavedDocument)))
            {
                using enum ContentObject::Reference::Types;
                if (!Content.IncomingReferences.empty())
                    I::PopStyleColor();
                I::SetNextWindowSizeConstraints({ }, { FLT_MAX, 300 });
                if (scoped::Child("Scroll", { }, ImGuiChildFlags_AutoResizeY))
                if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
                for (auto const& [object, type] : Content.IncomingReferences)
                {
                    DrawContentButton(object, &object, { .Icon = type == Root ? ICON_FA_ARROW_TURN_LEFT_UP : type == Tracked ? ICON_FA_CHEVRONS_LEFT : ICON_FA_ARROW_LEFT });
                    if (object->Root)
                        if (scoped::Indent(20))
                        DrawContentButton(object->Root, &type, { .Icon = ICON_FA_ARROW_TURN_LEFT_UP });
                }
            }
            else if (!Content.IncomingReferences.empty())
                I::PopStyleColor();
        }
    }

    static std::optional<HexViewerCellInfo> persistentHovered;
    static std::optional<std::tuple<TypeInfo::LayoutStack, std::string, uint32, bool, bool>> creatingSymbol;
    static std::optional<std::tuple<TypeInfo::LayoutStack, std::string, TypeInfo::Symbol*, ImVec2, ImVec2, bool>> editingSymbol;
    struct ViewUniqueValues
    {
        struct CachedKey
        {
            std::span<byte const> Data;
            TypeInfo::SymbolType const* Type = nullptr;

            auto operator<=>(CachedKey const& other) const
            {
                return Type && Type == other.Type
                    ? Type->CompareDataForSearch(Data.data(), other.Data.data())
                    : std::lexicographical_compare_three_way(Data.rbegin(), Data.rend(), other.Data.rbegin(), other.Data.rend());
            }
        };
        struct CachedValue
        {
            TypeInfo::Symbol Symbol;
            byte const* Data;
            std::set<ContentObject*> Objects;
            bool IsFolded = true;
        };

        ContentTypeInfo const& Type;
        std::string SymbolPath;
        bool IsEnum = false;
        bool IncludeZero = false;
        bool AsFlags = false;
        std::map<CachedKey, CachedValue> Results;
        std::set<TypeInfo::Condition::ValueType> ExternalKeyStorage;

        void Get(TypeInfo::Symbol const& symbol, TypeInfo::LayoutStack const& layoutStack)
        {
            SymbolPath = symbol.GetFullPath(*layoutStack.top().Path);
            IsEnum = symbol.GetEnum();
            IncludeZero = IsEnum && !symbol.GetEnum()->Flags;
            AsFlags = IsEnum && symbol.GetEnum()->Flags;
            Refresh();
        }
        void Refresh()
        {
            std::vector<std::string_view> path;
            for (auto const part : std::views::split(SymbolPath, std::string_view("->")))
                path.emplace_back(part);

            Results.clear();
            ExternalKeyStorage.clear();
            for (auto* object : Type.Objects)
            {
                for (auto& result : QuerySymbolData(*object, path))
                {
                    if (CachedKey key { { result.Data, result.Symbol->Size() }, result.Symbol->GetType() }; IncludeZero || std::ranges::any_of(key.Data, std::identity())) // Only show non-zero values
                    {
                        if (auto const e = result.Symbol->GetEnum(); e && e->Flags && AsFlags)
                        {
                            if (auto value = key.Type->GetValueForCondition(result.Data).value_or(0))
                            {
                                for (decltype(value) flag = 1; flag; flag <<= 1)
                                {
                                    if (value & flag)
                                    {
                                        auto data = (byte const*)&*ExternalKeyStorage.emplace(flag).first;
                                        Results.try_emplace({ { data, sizeof(decltype(ExternalKeyStorage)::value_type) } }, *result.Symbol, data).first->second.Objects.emplace(object);
                                    }
                                }
                                continue;
                            }
                        }
                        Results.try_emplace(key, *result.Symbol, result.Data).first->second.Objects.emplace(object);
                    }
                }
            }
        }
    };
    static std::optional<ViewUniqueValues> viewUniqueValues;
    static std::optional<uint32> highlightOffset;
    static std::optional<byte const*> highlightPointer;
    using DrawType = TypeInfo::Symbol::DrawType;
    ImGuiID tableLayoutScope = I::GetCurrentWindow()->GetID("TableLayoutScope");
    ImGuiID popupDefineStructLayoutSymbol = I::GetCurrentWindow()->GetID("DefineStructLayoutSymbol");
    ImGuiID popupChangeStructLayoutSymbol = I::GetCurrentWindow()->GetID("ChangeStructLayoutSymbol");

    struct Pointer
    {
        TypeInfo::Symbol* Symbol;
        uint32 Size;
        uint32 ArraySize;
        uint32 Index;
        std::string ParentPath;
    };
    using Pointers = std::map<byte const*, std::list<Pointer>>;
    struct RenderContentEditorContext
    {
        DrawType Draw;
        std::span<byte const> FullData;
        std::span<byte const> ScopedData;
        uint32 ScopeOffset;
        TypeInfo::StructLayout* Layout;
        ContentObject* Content;
        TypeInfo::LayoutStack TreeLayoutStack;
        std::vector<Pointers*> PointersStack;
        std::vector<std::tuple<std::string, uint32>> OutTableColumns;
    };
    auto renderContentEditor = [&typeInfo, tableLayoutScope, popupChangeStructLayoutSymbol, popupDefineStructLayoutSymbol](RenderContentEditorContext& context, auto& renderContentEditor) -> void
    {
        auto const& data = context.ScopedData;

        constexpr int INDENT = 30;
        TypeInfo::LayoutStack layoutStack;
        layoutStack.emplace(context.Content, context.Layout, context.TreeLayoutStack.top().Path);

        Pointers pointers;
        ImVec2 highlightPointerCursor;
        uint32 i = 0;
        uint32 unmappedStart = 0;
        auto flushHexEditor = [&]
        {
            if (i > unmappedStart)
            {
                auto const& frame = layoutStack.top();
                if (frame.IsFolded)
                    return;

                bool const empty = std::ranges::none_of(data.subspan(unmappedStart, i - unmappedStart), std::identity());
                switch (context.Draw)
                {
                    case DrawType::TableCountColumns:
                        context.OutTableColumns.emplace_back("Unmapped", 180);
                        return;
                    case DrawType::TableHeader:
                        I::TableNextColumn();
                        if (scoped::Disabled(true))
                            I::ButtonEx("<c=#444>" ICON_FA_GEAR "</c>");
                        I::SameLine(0, 0);
                        I::AlignTextToFramePadding();
                        I::SetCursorPosY(I::GetCursorPosY() + I::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
                        if (scoped::WithColorVar(ImGuiCol_Text, { 1, 1, 1, 0.25f }))
                            I::TableHeader(empty ? std::format("###{}", I::TableGetColumnName()).c_str() : I::TableGetColumnName());
                        return;
                    case DrawType::TableRow:
                        I::TableNextColumn();
                        if (empty)
                            return I::TextUnformatted("<c=#444>" ICON_FA_0 "</c>");
                        [[fallthrough]];
                    case DrawType::Inline:
                        break;
                }

                HexViewerOptions options
                {
                    .StartOffset = unmappedStart + context.ScopeOffset,
                    .ShowHeaderRow = false,
                    .ShowOffsetColumn = context.Draw != DrawType::TableRow,
                    .ShowVerticalScroll = context.Draw != DrawType::TableRow,
                    .OutHighlightOffset = highlightOffset,
                    .OutHighlightPointer = highlightPointer,
                    .ByteMap = context.Content ? context.Content->ByteMap : nullptr,
                };
                DrawHexViewer(data.subspan(unmappedStart, i - unmappedStart), options);
                if (auto const offset = options.OutHighlightOffset)
                    highlightOffset = offset;
                else if (options.OutHoveredInfo)
                    highlightOffset.reset();
                if (auto const pointer = options.OutHighlightPointer)
                    highlightPointer = pointer;
                else if (options.OutHoveredInfo)
                    highlightPointer.reset();
                if (options.OutHoveredInfo && I::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    persistentHovered = options.OutHoveredInfo;
                    creatingSymbol = { g_config.TreeContentStructLayout ? context.TreeLayoutStack : layoutStack, *layoutStack.top().Path, frame.DataStart + context.ScopeOffset, false, false };
                    I::OpenPopup(popupDefineStructLayoutSymbol);
                }
            }
        };

        bool lastFrameWasFolded = false;
        while (i <= data.size())
        {
            auto* p = data.data() + i;
            if (auto itr = pointers.find(p); itr != pointers.end())
            {
                for (auto const& pointer : itr->second)
                {
                    //if (!g_config.TreeContentStructLayout)
                    if (!(layoutStack.size() > 1 && g_config.TreeContentStructLayout))
                        flushHexEditor();
                    
                    auto const symbolType = pointer.Symbol->GetType();
                    bool const isContentPointer = symbolType->IsContent();
                    auto* layout = [&]() -> TypeInfo::StructLayout*
                    {
                        if (isContentPointer)
                        {
                            if (auto const content = GetContentObjectByDataPointer(data.data() + (pointer.Size ? i : layoutStack.top().ObjectStart)))
                            {
                                auto& elementTypeInfo = g_config.TypeInfo.try_emplace(content->Type->Index).first->second;
                                elementTypeInfo.Initialize(*content->Type);
                                return &elementTypeInfo.Layout;
                            }

                            return nullptr;
                        }

                        return &pointer.Symbol->GetElementLayout();
                    }();
                    if (pointer.Size)
                    {
                        if (!pointer.ArraySize || !pointer.Index)
                        {
                            if (g_config.TreeContentStructLayout)
                            {
                                I::TextColored({ 1, 1, 1, 0.1f }, ICON_FA_EYE_SLASH " %u bytes omitted: " ICON_FA_FOLDER_TREE " %s", pointer.Size * std::max(1u, pointer.ArraySize), pointer.Symbol->GetFullPath(pointer.ParentPath).c_str());
                            }
                            else
                            {
                                scoped::WithID(context.ScopeOffset + i);
                                if (!lastFrameWasFolded)
                                    I::Dummy({ 1, 10 });
                                if (highlightPointer == p)
                                    highlightPointerCursor = I::GetCursorScreenPos();
                                if (I::Button(pointer.Symbol->Folded ? ICON_FA_CHEVRON_RIGHT "##Fold" : ICON_FA_CHEVRON_DOWN "##Fold"))
                                    pointer.Symbol->Folded ^= true;
                                I::SameLine(0, 0);
                                I::Text("%s->", pointer.Symbol->GetFullPath(pointer.ParentPath).c_str());
                                I::SameLine();
                                if (!pointer.Symbol->Autogenerated/* && context.Draw != DrawType::TableRow ???*/)
                                {
                                    bool const edit = I::Button(std::format("<c=#{}>" ICON_FA_GEAR "</c>##EditFromListTarget", pointer.Symbol->Condition.has_value() ? "FF0" : std::ranges::contains(typeInfo.NameFields, pointer.Symbol->GetFullPath(pointer.ParentPath)) ? "FEA" : std::ranges::contains(typeInfo.IconFields, pointer.Symbol->GetFullPath(pointer.ParentPath)) ? "EAF" : "FFF").c_str());
                                    I::SameLine(0, 0);

                                    if (edit)
                                    {
                                        editingSymbol = { g_config.TreeContentStructLayout ? context.TreeLayoutStack : layoutStack, pointer.ParentPath, pointer.Symbol, I::GetCurrentContext()->LastItemData.Rect.GetBL(), I::GetCurrentContext()->LastItemData.Rect.GetTL(), false };
                                        I::OpenPopup(popupChangeStructLayoutSymbol);
                                    }
                                }
                                I::SetNextItemWidth(120);
                                I::DragCoerceInt("##ElementSizeFromListTarget", (int*)&pointer.Symbol->ElementSize, 0.1f, 1, 10000, "Size: %u bytes", ImGuiSliderFlags_AlwaysClamp, [](auto v) { return std::max(1, (v / 4) * 4); });
                                I::Indent(INDENT);
                            }
                            auto const& top = layoutStack.top();
                            layoutStack.emplace(
                                context.Content,
                                layout,
                                pointer.Symbol->GetFullPath(pointer.ParentPath),
                                i,
                                isContentPointer ? i : top.ObjectStart,
                                isContentPointer ? layoutStack.size() : top.ObjectStackDepth,
                                pointer.Symbol->Folded);
                        }
                        if (pointer.ArraySize)
                        {
                            if (auto& frame = layoutStack.top(); frame.Layout == layout)
                            {
                                frame.DataStart = i;
                                if (!frame.IsFolded && !g_config.TreeContentStructLayout)
                                {
                                    I::Unindent(INDENT);
                                    I::TextColored({ 1, 1, 1, 0.25f }, "[<c=#FFF>%u</c>]", pointer.Index);
                                    I::SameLine();
                                    I::Indent(INDENT);
                                }
                            }
                        }
                    }
                    else
                    {
                        if (auto const& frame = layoutStack.top(); !pointer.ArraySize || frame.Layout == layout)
                        {
                            if (!g_config.TreeContentStructLayout)
                            {
                                I::Unindent(INDENT);
                                if (highlightPointer == data.data() + frame.DataStart)
                                    I::GetWindowDrawList()->AddLine(highlightPointerCursor, I::GetCursorScreenPos(), 0xFF0000FF, 2);
                                lastFrameWasFolded = frame.IsFolded;
                                if (!lastFrameWasFolded)
                                    I::Dummy({ 1, 10 });
                            }
                            layoutStack.pop();
                        }
                    }
                    unmappedStart = i;
                }
            }
            if (layoutStack.empty())
                break;
            auto const& frame = layoutStack.top();
            if (!frame.Layout)
            {
                ++i;
                continue;
            }
            bool render = !(layoutStack.size() > 1 && g_config.TreeContentStructLayout);

            auto& layout = frame.Layout->Symbols;
            auto typeLocalOffset = i - frame.DataStart;
            if (auto itr = layout.find(typeLocalOffset); itr != layout.end() || i == data.size())
            {
                if (itr != layout.end())
                {
                    itr = layout.end();
                    auto [candidateFrom, candidateTo] = layout.equal_range(typeLocalOffset);
                    for (auto candidateItr = candidateFrom; candidateItr != candidateTo && itr == layout.end(); ++candidateItr)
                    {
                        auto const& symbol = candidateItr->second;
                        if (symbol.Condition && !symbol.Condition->Field.empty())
                        {
                            if (context.Content && candidateItr->second.TestCondition(*context.Content, g_config.TreeContentStructLayout && context.TreeLayoutStack.size() > 1 ? context.TreeLayoutStack : layoutStack))
                                itr = candidateItr;
                        }
                        else
                            itr = candidateItr;
                    }

                    if (itr == layout.end())
                    {
                        ++i;
                        continue;
                    }
                }

                if (render)
                    flushHexEditor();

                if (itr != layout.end())
                {
                    auto& symbol = itr->second;

                    if (highlightOffset && *highlightOffset == i)
                        I::GetForegroundDrawList()->AddLine(I::GetCursorScreenPos(), I::GetCursorScreenPos() + ImVec2(0, 16), 0xFFFF0000, 2);
                    if (highlightPointer && *highlightPointer == p)
                        I::GetForegroundDrawList()->AddLine(I::GetCursorScreenPos(), I::GetCursorScreenPos() + ImVec2(0, 16), 0xFF0000FF, 2);

                    auto cursor = I::GetCursorScreenPos();
                    if (render && !frame.IsFolded)
                    {
                        switch (context.Draw)
                        {
                            case DrawType::TableCountColumns:
                                context.OutTableColumns.emplace_back(symbol.Name, 0);
                                render = false;
                                break;
                            case DrawType::TableHeader:
                                render = false;
                            case DrawType::TableRow:
                                I::TableNextColumn();
                                [[fallthrough]];
                            case DrawType::Inline:
                                if (!symbol.Autogenerated && context.Draw != DrawType::TableRow)
                                {
                                    scoped::WithID(context.ScopeOffset + i);
                                    bool const edit = I::Button(std::format("<c=#{}>" ICON_FA_GEAR "</c>##Edit", symbol.Condition.has_value() ? "FF0" : std::ranges::contains(typeInfo.NameFields, symbol.GetFullPath(*frame.Path)) ? "FEA" : std::ranges::contains(typeInfo.IconFields, symbol.GetFullPath(*frame.Path)) ? "EAF" : "FFF").c_str());
                                    I::SameLine(0, 0);

                                    if (edit)
                                    {
                                        editingSymbol = { g_config.TreeContentStructLayout ? context.TreeLayoutStack : layoutStack, *frame.Path, &symbol, I::GetCurrentContext()->LastItemData.Rect.GetBL(), I::GetCurrentContext()->LastItemData.Rect.GetTL(), false };
                                        I::OpenPopup(popupChangeStructLayoutSymbol);
                                    }
                                }
                                symbol.Draw(p, context.Draw, *context.Content);
                                break;
                        }
                    }

                    uint32 const symbolSize = symbol.Size();
                    uint32 const symbolAlignedSize = symbol.AlignedSize();

                    if (render && i + symbolSize > data.size())
                        I::GetWindowDrawList()->AddRectFilled(cursor, I::GetCurrentContext()->LastItemData.Rect.GetBR(), 0x400000FF);

                    if (render && symbolSize != symbolAlignedSize && std::ranges::any_of(p + symbolSize, p + symbolAlignedSize, std::identity()))
                        I::GetWindowDrawList()->AddRectFilled(cursor, I::GetCurrentContext()->LastItemData.Rect.GetBR(), 0x400080FF);

                    unmappedStart = i += symbolAlignedSize;

                    if (auto const traversal = symbol.GetTraversalInfo(p))
                    {
                        if (render && I::IsMouseHoveringRect(cursor, I::GetCurrentContext()->LastItemData.Rect.GetBR()))
                            highlightPointer = *traversal.Start;

                        auto const elements = std::span(*traversal.Start, context.FullData.data() + context.FullData.size()) | std::views::stride(traversal.Size) | std::views::take(traversal.ArrayCount.value_or(1)) | std::views::enumerate;

                        auto markPointers = [&elements, &symbol, &traversal, &frame](auto& pointers)
                        {
                            for (auto [index, element] : elements)
                                pointers[&element].emplace_back(&symbol, traversal.Size, traversal.ArrayCount.value_or(0), index, *frame.Path);
                            pointers[*traversal.Start + traversal.Size * traversal.ArrayCount.value_or(1)].emplace_front(&symbol, 0, traversal.ArrayCount.value_or(0), -1, *frame.Path);
                        };
                        markPointers(pointers);
                        for (auto parentPointers : context.PointersStack)
                            markPointers(*parentPointers);

                        if (render && g_config.TreeContentStructLayout && traversal.Type->IsInline())
                        {
                            scoped::WithID(context.ScopeOffset + i);
                            if (I::Button(symbol.Folded ? ICON_FA_CHEVRON_RIGHT "##Fold" : ICON_FA_CHEVRON_DOWN "##Fold"))
                                symbol.Folded ^= true;
                            if (symbol.Folded)
                            {
                                I::SameLine();
                                I::TextColored({ 1, 1, 1, 0.25f }, "%u bytes folded", traversal.Size * traversal.ArrayCount.value_or(1));
                            }
                            else
                            {
                                auto const rect = I::GetCurrentContext()->LastItemData.Rect;
                                I::SameLine(-1, 1);
                                bool table = false;
                                for (auto [index, element] : elements)
                                {
                                    auto const target = &element;
                                    if (scoped::Indent(rect.GetWidth()))
                                    {
                                        if (traversal.ArrayCount && !symbol.Table)
                                        {
                                            I::TextColored({ 1, 1, 1, 0.25f }, "[<c=#FFF>%u</c>]", index);
                                            auto const text = I::GetCurrentContext()->LastItemData.Rect;
                                            if (index)
                                                I::GetWindowDrawList()->AddLine(
                                                    { (float)(int)((rect.Min.x + rect.Max.x) / 2) + 1, (float)(int)((text.Min.y + text.Max.y) / 2) },
                                                    { (float)(int)rect.Max.x, (float)(int)((text.Min.y + text.Max.y) / 2) },
                                                    0x40FFFFFF);
                                            I::SameLine();
                                            I::Indent(INDENT);
                                        }
                                        do
                                        {
                                            RenderContentEditorContext options
                                            {
                                                .Draw = DrawType::Inline,
                                                .FullData = context.FullData,
                                                .ScopedData = { target, traversal.Size },
                                                .ScopeOffset = context.ScopeOffset + (uint32)std::distance(data.data(), target),
                                                .Layout = &symbol.GetElementLayout(),
                                                .Content = context.Content,
                                                .TreeLayoutStack = context.TreeLayoutStack,
                                                .PointersStack = context.PointersStack,
                                            };
                                            if (traversal.Type->IsContent())
                                            {
                                                if (auto const content = GetContentObjectByDataPointer(&element))
                                                {
                                                    content->Finalize();
                                                    auto& elementTypeInfo = g_config.TypeInfo.try_emplace(content->Type->Index).first->second;
                                                    elementTypeInfo.Initialize(*content->Type);

                                                    options.ScopedData = /*options.FullData = - deliberately not modified*/ { content->Data.data(), traversal.Size };
                                                    options.ScopeOffset = 0;
                                                    options.Layout = &elementTypeInfo.Layout;
                                                    options.Content = content;
                                                }
                                                else
                                                    break;
                                            }
                                            // WTB: span intersection
                                            if (!(options.ScopedData.data() >= options.FullData.data() && options.ScopedData.data() < options.FullData.data() + options.FullData.size()) ||
                                                !(options.ScopedData.data() + options.ScopedData.size() >= options.FullData.data() && options.ScopedData.data() + options.ScopedData.size() <= options.FullData.data() + options.FullData.size()))
                                            {
                                                I::TextColored({ 1, 0, 0, 1 }, "OUT OF BOUNDS");
                                                break;
                                            }
                                            options.TreeLayoutStack.emplace(options.Content, options.Layout, symbol.GetFullPath(*options.TreeLayoutStack.top().Path), options.ScopeOffset);
                                            options.PointersStack.emplace_back(&pointers);
                                            if (symbol.Table)
                                            {
                                                if (!index)
                                                {
                                                    options.Draw = DrawType::TableCountColumns;
                                                    renderContentEditor(options, renderContentEditor);

                                                    //if (scoped::OverrideID(tableLayoutScope)) // Make all symbols of the same type share table settings
                                                    //if (scoped::WithID(&symbol))
                                                    {
                                                        I::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2());
                                                        I::BeginTableEx("SymbolTable", (ImGuiID)&symbol, options.OutTableColumns.size(), ImGuiTableFlags_Borders | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoSavedSettings);
                                                        table = true;
                                                        for (size_t column = 0; auto const& [name, width] : options.OutTableColumns)
                                                            I::TableSetupColumn(std::format("{}##{}", name, column++).c_str(), ImGuiTableColumnFlags_WidthFixed, width);
                                                    }
                                                    I::TableNextRow(ImGuiTableRowFlags_Headers);
                                                    options.Draw = DrawType::TableHeader;
                                                    renderContentEditor(options, renderContentEditor);
                                                }
                                                I::TableNextRow();
                                                options.Draw = DrawType::TableRow;
                                            }
                                            renderContentEditor(options, renderContentEditor);
                                        }
                                        while (false);
                                        if (traversal.ArrayCount && !symbol.Table)
                                            I::Unindent(INDENT);
                                    }
                                }
                                if (table)
                                //if (scoped::OverrideID(tableLayoutScope)) // Make all symbols of the same type share table settings
                                //if (scoped::WithID(&symbol))
                                {
                                    I::EndTable();
                                    I::PopStyleVar();
                                }

                                I::GetWindowDrawList()->AddLine(
                                    { (float)(int)((rect.Min.x + rect.Max.x) / 2), (float)(int)rect.Max.y },
                                    { (float)(int)((rect.Min.x + rect.Max.x) / 2), (float)(int)I::GetCursorScreenPos().y },
                                    0x40FFFFFF);
                            }
                        }
                    }
                }
                else
                    ++i;
            }
            else
                ++i;
        }
    };

    if (scoped::Child("Scroll", { }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, { I::GetStyle().ItemSpacing.x, 0 }))
    {
        RenderContentEditorContext options
        {
            .Draw = DrawType::Inline,
            .FullData = Content.Data,
            .ScopedData = Content.Data,
            .ScopeOffset = 0,
            .Layout = &typeInfo.Layout,
            .Content = &Content,
        };
        options.TreeLayoutStack.emplace(options.Content, options.Layout, "", options.ScopeOffset);
        renderContentEditor(options, renderContentEditor);
    }

    if (creatingSymbol && persistentHovered)
    {
        auto& [layoutStack, parentPath, typeStartOffset, initialized, up] = *creatingSymbol;
        static TypeInfo::Symbol symbol;
        if (symbol.Type.empty())
            symbol.Type = "uint32";

        auto const& [byteOffset, cellCursor, cellSize, tableCursor, tableSize] = *persistentHovered;
        auto offset = byteOffset - typeStartOffset;
        I::GetWindowDrawList()->AddRectFilled(ImVec2(tableCursor.x, cellCursor.y + 2), ImVec2(tableCursor.x + tableSize.x, cellCursor.y + cellSize.y - 2), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.2f }));
        I::GetWindowDrawList()->AddRectFilled(ImVec2(cellCursor.x + 2, tableCursor.y), ImVec2(cellCursor.x + cellSize.x - 2, tableCursor.y + tableSize.y), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.2f }));
        I::GetWindowDrawList()->AddRect(cellCursor, cellCursor + ImVec2(cellSize.x * symbol.Size(), cellSize.y), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.5f }));
        I::GetWindowDrawList()->AddRectFilled(cellCursor + ImVec2(cellSize.x * symbol.Size(), 0), cellCursor + ImVec2(cellSize.x * symbol.AlignedSize(), cellSize.y), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.5f }));

        I::SetNextWindowPos(cellCursor + ImVec2(0, up ? 0 : cellSize.y), ImGuiCond_None, { 0, up ? 1.0f : 0.0f });
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, { I::GetStyle().ItemSpacing.x, 0 }))
        if (scoped::Popup("DefineStructLayoutSymbol", ImGuiWindowFlags_NoDecoration))
        {
            auto& layout = layoutStack.top().Layout->Symbols;
            if (!initialized)
            {
                initialized = true;
                static bool symbolWasCopyingUnion = false;

                if (auto itr = std::ranges::find_if(layout, [offset](auto const& pair) { return pair.first == offset; }); itr != layout.end())
                {
                    symbolWasCopyingUnion = true;
                    auto const& existing = itr->second;
                    symbol.Condition = existing.Condition;
                    symbol.Name = existing.Name;
                    symbol.Type = existing.Type;
                    symbol.Enum = existing.Enum;
                    symbol.Alignment = existing.Alignment;
                    if (symbol.Condition && !symbol.Condition->Field.empty())
                    {
                        if (auto const value = symbol.GetValueForCondition(Content, layoutStack))
                            symbol.Condition->Value = *value;
                        else
                            symbol.Condition->Value = { };
                    }
                }
                else if (symbolWasCopyingUnion)
                {
                    symbolWasCopyingUnion = false;
                    symbol.Condition.reset();
                    symbol.Name.clear();
                    symbol.Enum.reset();
                    symbol.Alignment = { };
                }
            }

            I::Text("Define symbol at offset 0x%X (%d)", offset, offset);
            auto const placeholderName = std::format("field{:X}", offset);
            symbol.DrawOptions(typeInfo, layoutStack, { }, true, placeholderName);

            bool close = false;
            if (I::Button(ICON_FA_PLUS " Define"))
            {
                auto& added = layout.emplace(offset, symbol)->second;
                //added.Parent = layoutStack.top().Layout->Parent;
                if (added.Name.empty())
                    added.Name = placeholderName;
                //added.FinishLoading();

                symbol.Name.clear();
                symbol.Condition.reset();

                close = true;
            }

            if (I::SameLine(); I::Button("View Unique Values"))
            {
                auto itr = layout.emplace(offset, symbol);
                auto& added = itr->second;
                //added.Parent = layoutStack.top().Layout->Parent;
                if (added.Name.empty())
                    added.Name = placeholderName;
                //added.FinishLoading();

                viewUniqueValues.emplace(*Content.Type).Get(added, layoutStack);

                layout.erase(itr);
            }

            if (I::GetWindowPos().y + I::GetWindowSize().y > I::GetIO().DisplaySize.y)
                up = true;
            if (I::SameLine(); I::Button("Cancel") || close)
            {
                persistentHovered.reset();
                I::CloseCurrentPopup();
            }
        }
        else
            persistentHovered.reset();
    }
    if (editingSymbol)
    {
        auto& [layoutStack, parentPath, symbol, cursorBL, cursorTL, up] = *editingSymbol;
        auto& layout = layoutStack.top().Layout->Symbols;
        auto symbolItr = std::ranges::find_if(layout, [symbol](auto const& pair) { return &pair.second == symbol; });
        I::SetNextWindowPos(up ? cursorTL : cursorBL, ImGuiCond_None, { 0, up ? 1.0f : 0.0f });
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, { I::GetStyle().ItemSpacing.x, 0 }))
        if (scoped::Popup("ChangeStructLayoutSymbol", ImGuiWindowFlags_NoDecoration))
        {
            I::Text("Editing symbol at offset 0x%X (%d)", symbolItr->first, symbolItr->first);
            I::TextUnformatted(symbol->GetFullPath(parentPath).c_str());
            symbol->DrawOptions(typeInfo, layoutStack, parentPath, false, symbolItr != layout.end() ? std::format("field{:X}", symbolItr->first) : "");
            bool close = I::Button("Close");

            if (I::SameLine(); I::Button("View Unique Values"))
                viewUniqueValues.emplace(*Content.Type).Get(*symbol, layoutStack);

            if (symbolItr != layout.end())
            if (scoped::WithColorVar(ImGuiCol_Text, 0xFF0000FF))
            if (I::SameLine(); I::Button(ICON_FA_XMARK " Undefine"))
            {
                layout.erase(symbolItr);
                close = true;
            }

            if (I::GetWindowPos().y + I::GetWindowSize().y > I::GetIO().DisplaySize.y)
                up = true;
            if (close)
            {
                editingSymbol.reset();
                I::CloseCurrentPopup();
            }
        }
        else
            editingSymbol.reset();
    }
    if (viewUniqueValues)
    {
        bool open = true;
        if (scoped::Window("View Unique Field Values", &open, ImGuiWindowFlags_NoFocusOnAppearing))
        {
            if (I::Button(ICON_FA_ARROWS_ROTATE " Refresh"))
                viewUniqueValues->Refresh();
            if (I::SameLine(), I::Checkbox("Include Zero", &viewUniqueValues->IncludeZero))
                viewUniqueValues->Refresh();
            if (viewUniqueValues->IsEnum && (I::SameLine(), I::Checkbox("As Flags", &viewUniqueValues->AsFlags)))
                viewUniqueValues->Refresh();

            if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
            if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
            if (scoped::Table("UniqueFieldValues", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings))
            {
                I::TableSetupColumn(viewUniqueValues->SymbolPath.c_str(), 0, 1);
                I::TableSetupColumn("##Fold", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
                I::TableSetupColumn("Content", 0, 3);
                I::TableSetupScrollFreeze(0, 1);
                I::TableHeadersRow();

                auto const tableContentsCursor = I::GetCursorScreenPos();

                ImGuiListClipper clipper;
                clipper.Begin(std::ranges::fold_left(viewUniqueValues->Results, 0u, [](uint32 count, auto const& pair) { return count + (pair.second.IsFolded ? 1 : pair.second.Objects.size()); })/*, I::GetFrameHeight()*/);
                std::set<decltype(ViewUniqueValues::Results)::key_type> keyDrawn;
                while (clipper.Step())
                {
                    int drawn = 0;
                    int offset = 0;
                    for (auto& [key, value] : viewUniqueValues->Results)
                    {
                        int numToDisplay = value.IsFolded ? 1 : value.Objects.size();
                        auto displayedObjects = value.Objects | std::views::take(numToDisplay) | std::views::drop(std::max(0, clipper.DisplayStart - offset)) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart - drawn);
                        bool const canAdjustY = !value.IsFolded && displayedObjects.size() > 1;
                        bool first = !keyDrawn.contains(key);
                        for (auto* object : displayedObjects)
                        {
                            I::TableNextRow();

                            float const yOffset = canAdjustY && I::GetCursorScreenPos().y < tableContentsCursor.y ? tableContentsCursor.y - I::GetCursorScreenPos().y : 0;

                            I::TableNextColumn();
                            if (first)
                            {
                                if (yOffset)
                                    I::SetCursorPosY(I::GetCursorPosY() + yOffset);
                                value.Symbol.Draw(value.Data, DrawType::TableRow, *object);
                                I::GetCurrentWindow()->DC.CursorMaxPos.y -= yOffset;
                            }

                            I::TableNextColumn();
                            if (first && value.Objects.size() > 1)
                            {
                                if (yOffset)
                                    I::SetCursorPosY(I::GetCursorPosY() + yOffset);
                                if (scoped::WithID(&value))
                                    if (I::Button(value.IsFolded ? ICON_FA_CHEVRON_RIGHT : ICON_FA_CHEVRON_DOWN))
                                        value.IsFolded ^= true;
                                I::GetCurrentWindow()->DC.CursorMaxPos.y -= yOffset;
                            }

                            I::TableNextColumn();
                            DrawContentButton(object, &object);
                            if (first)
                            {
                                keyDrawn.emplace(key);
                                first = false;
                            }
                            ++drawn;
                        }
                        offset += numToDisplay;
                    }
                }
            }
        }
        if (!open)
            viewUniqueValues.reset();
    }
}

struct UI::ConversationViewer : Viewer
{
    uint32 ConversationID;
    std::stack<uint32> HistoryPrev;
    std::stack<uint32> HistoryNext;

    std::optional<uint32> EditingScriptedStartTransitionStateID;
    bool EditingScriptedStartTransitionFocus = false;

    ConversationViewer(uint32 id, bool newTab, uint32 conversationID) : Viewer(id, newTab), ConversationID(conversationID) { }

    std::string Title() override
    {
        return std::format("<c=#4>Conversation #</c>{}", ConversationID);
    }
    void Draw() override;
};
void UI::ConversationViewer::Draw()
{
    // OLD: static constexpr uint32 icons[] { 156129, 156127, 156131, 156137, 156145, 156146, 156132, 156128, 156139, 156138, 156148, 156149, 156126, 156130, 156133, 156134, 156135, 156136, 567512, 567513, 156143, 156144, 156147, 156150, 156151, 156153, 1228228, 1973946, 0 };
    // NEW: static constexpr uint32 iconFileIDs[] { 156127, 156128, 156129, 156131, 156137, 156138, 156139, 156145, 156146, 156132, 156148, 156149, 156126, 156130, 156133, 156134, 156135, 156136, 567512, 567513, 156143, 156144, 3621055, 156147, 156150, 156151, 156153, 1228228, 1973946, 0 };
    static constexpr uint32 iconToIconInfoIndex[] { 2, 0, 3, 4, 7, 8, 9, 1, 6, 5, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 21, 23, 24, 25, 26, 27, 28, 29 };
    static constexpr struct 
    {
        uint32 FileID;
        std::string_view Wiki;
    } iconInfo[]
    {
        { 156127, "charisma" },
        { 156128, "ferocity" },
        { 156129, "charisma" },
        { 156131, "charisma" },
        { 156137, "dignity" },
        { 156138, "ferocity" },
        { 156139, "ferocity" },
        { 156145, "dignity" },
        { 156146, "dignity" },
        { 156132, "ready" },
        { 156148, "question mark" },
        { 156149, "tick" },
        { 156126, "back" },
        { 156130, "more" },
        { 156133, "end" },
        { 156134, "more" },
        { 156135, "combat" },
        { 156136, "choice" },
        { 567512, "ls talk" },
        { 567513, "recap" },
        { 156143, "give" },
        { 156144, "story" },
        { 3621055, "???????????????????????????????????????????????????????" },
        { 156147, "more" },
        { 156150, "sell" },
        { 156151, "karma" },
        { 156153, "yes" },
        { 1228228, "legendary weapon" },
        { 1973946, "collection" },
        { 0, "???????????????????????????????????????????????????????" },
    };

    std::shared_lock _(conversationsLock);
    auto& conversation = conversations.at(ConversationID);
    bool const conversationHasMultipleSpeakers = conversation.HasMultipleSpeakers();
    bool const conversationHasScriptedStart = conversation.HasScriptedStart();

    bool wikiWrite = false;
    uint32 wikiDepth = 0;
    std::string wikiBuffer;
    auto wiki = std::back_inserter(wikiBuffer);
    if (I::Button(ICON_FA_COPY " Wiki Markup"))
    {
        wikiWrite = true;
        wikiBuffer.reserve(64 * 1024);
    }
    static bool drawStateTypeIcons = true, drawStateTypeText = false, drawSpeakerName = false, drawEncryptionStatus = false, drawTextID = false, drawEncounterInfo = false;
    I::SameLine(); I::Checkbox("State Type Icons", &drawStateTypeIcons);
    I::SameLine(); I::Checkbox("State Type Text", &drawStateTypeText);
    I::SameLine(); I::Checkbox("Speaker Name", &drawSpeakerName);
    I::SameLine(); I::Checkbox("Text ID", &drawTextID);
    I::SameLine(); I::Checkbox("Encryption Status", &drawEncryptionStatus);
    I::SameLine(); I::Checkbox("Encounter Info", &drawEncounterInfo);

    auto drawState = [&](bool parentOpen, Conversation::State const& state, std::set<uint32>& visitedStates, uint32& startingSpeakerNameTextID, auto& drawState) -> void
    {
        if (!startingSpeakerNameTextID)
            startingSpeakerNameTextID = state.SpeakerNameTextID;

        bool const isScriptedStartState = conversationHasScriptedStart && state.IsScriptedStateState();
        if (isScriptedStartState)
        {
            std::set targets { std::from_range, conversation.States | std::views::transform(&Conversation::State::StateID) };
            targets.erase(state.StateID);
            for (auto const& state : conversation.States)
                for (auto const& transition : state.Transitions)
                    for (auto const& target : transition.Targets)
                        targets.erase(target.TargetStateID);

            uint32 i = 0;
            std::erase_if(state.ScriptedTransitions, [&targets](Conversation::State::Transition const& transition) { return !targets.contains(transition.Targets.begin()->TargetStateID); });
            for (auto const& target : targets)
            {
                state.ScriptedTransitions.emplace(Conversation::State::Transition
                {
                    .TransitionID = i++,
                    .TextID = 0,
                    .CostAmount = 0,
                    .CostType = 2,
                    .CostKarma = 0,
                    .Diplomacy = 9,
                    .Unk = 3,
                    .Personality = 10,
                    .Icon = std::size(iconToIconInfoIndex) - 1,
                    .SkillDefDataID = 0,
                    .Targets = { { .TargetStateID = target, .Flags = 0 } },
                });
            }
        }
        auto const& transitions = isScriptedStartState ? state.ScriptedTransitions : state.Transitions;

        auto [speaker, speakerStatus] = GetString(state.SpeakerNameTextID);
        auto [string, status] = GetString(state.TextID);
        std::string text = string ? to_utf8(*string).c_str() : "";
        replace_all(text, "\r", R"(<c=#F00>\r</c>)");
        replace_all(text, "\n", R"(<c=#F00>\n</c>)");

        bool wikiState = false;
        bool alreadyOpened = !visitedStates.emplace(state.StateID).second;
        bool const stateOpen = parentOpen && [&]
        {
            bool const isStart = state.IsStart() || isScriptedStartState;

            I::SetNextItemAllowOverlap();
            bool const open = I::TreeNodeEx(&state,
                ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | (transitions.empty() ? ImGuiTreeNodeFlags_Leaf : !alreadyOpened ? ImGuiTreeNodeFlags_DefaultOpen : 0),
                "<c=#4>[<c=#FFFF>%u</c>]</c><c=#%s><c=#8>%s</c><c=#4>%s</c></c>",
                state.StateID,
                isStart ? "0F0" : state.IsExit() ? "F00" : "FF0",
                drawStateTypeIcons ? " " ICON_FA_CIRCLE : "", // !drawStateTypeIcons ? "" : isStart ? " " ICON_FA_CIRCLE_PLAY : state.IsExit() ? " " ICON_FA_CIRCLE_X : " " ICON_FA_CIRCLE_RIGHT,
                !drawStateTypeText ? "" : isStart ? " Start State:" : state.IsExit() ? " Exit State" : " State:");

            if (scoped::ItemTooltip())
            {
                auto const drawImpl = [](char const* name, uint32 value, std::optional<uint32> def = { }, bool invert = false)
                {
                    I::Text(!def || (value == *def ^ invert) ? "%s: %u" : "<c=#F00>%s: %u</c>", name, value);
                };
                #define draw(field, ...) drawImpl(#field, state.##field, __VA_ARGS__)
                draw(StateID);
                draw(TextID, 0, true);
                draw(SpeakerNameTextID, 0, true);
                draw(SpeakerPortraitOverrideFileID, 0);
                draw(Priority, 0);
                draw(Flags, 0);
                draw(Voting, 0);
                draw(Timeout, 0);
                draw(CostAmount, 0);
                draw(CostType, 2);
                draw(Unk, 0);
                #undef draw
            }

            if (wikiWrite && (open || wikiDepth) && (!state.IsExit() || state.TextID) && !isScriptedStartState)
            {
                wikiState = true;

                if (conversationHasScriptedStart && !wikiDepth)
                {
                    char const* situation = "((Situation for this tree))";
                    if (auto const itr = g_config.ConversationScriptedStartSituations.find(ConversationID); itr != g_config.ConversationScriptedStartSituations.end())
                        if (auto const itr2 = itr->second.find(state.StateID); itr2 != itr->second.end())
                            situation = itr2->second.c_str();
                    std::format_to(wiki, "\r\n;{}:\r\n", situation);
                }

                ++wikiDepth;
                wikiBuffer.append(wikiDepth, ':');

                if (conversationHasMultipleSpeakers && state.SpeakerNameTextID)
                    std::format_to(wiki, "'''{}:''' ", *speaker);

                std::string text = string ? to_utf8(*string).c_str() : "";
                replace_all(text, "\r\n", "<br>");
                replace_all(text, "\r", "<br>");
                replace_all(text, "\n", "<br>");
                std::format_to(wiki, "{}\r\n", text);
            }

            return open;
        }();
        auto _ = gsl::finally([&]
        {
            if (wikiState)
                --wikiDepth;
        });
        if (parentOpen)
        {
            I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, state.GetCompleteness() / (float)Conversation::COMPLETENESS_COMPLETE)));

            if (drawEncounterInfo)
            {
                I::SameLine();
                if (state.EncounteredTime.time_since_epoch().count())
                {
                    if (I::Button(std::format("<c=#{}>{}</c> {}###EncounteredTime", state.Map ? "F" : "2", ICON_FA_GLOBE, format_duration_colored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - state.EncounteredTime))).c_str()))
                    {
                        // TODO: Open map to { state.Map, state.Position }
                    }
                    if (scoped::ItemTooltip())
                        I::TextUnformatted(std::format("Encountered on: {:%F %T}", std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(state.EncounteredTime))).c_str());
                }
            }

            if (state.SpeakerNameTextID != startingSpeakerNameTextID || drawSpeakerName)
            {
                I::SameLine();
                I::Text("<c=#8>[%s%s%s]</c>",
                    drawTextID ? std::format("<c=#CCF>({})</c> ", state.SpeakerNameTextID).c_str() : "",
                    drawEncryptionStatus ? GetEncryptionStatusText(speakerStatus) : "",
                    speaker ? to_utf8(*speaker).c_str() : "");
            }

            I::SameLine();
            I::Text("%s%s%s",
                drawTextID ? std::format("<c=#CCF>({})</c>", state.TextID).c_str() : "",
                drawEncryptionStatus ? GetEncryptionStatusText(status) : "", 
                text.c_str());
        }

        if (!stateOpen)
        {
            if (alreadyOpened)
                return;

            auto const id = I::GetCurrentWindow()->GetID(&state);
            I::TreeNodeUpdateNextOpen(id, transitions.empty() ? ImGuiTreeNodeFlags_Leaf : !alreadyOpened ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            I::TreePushOverrideID(id);
        }

        uint32 nextExpectedTransitionID = 0;
        for (auto const& transition : transitions)
        {
            if (stateOpen)
            {
                for (auto missingTransitionID = nextExpectedTransitionID; missingTransitionID < transition.TransitionID; ++missingTransitionID)
                {
                    if (I::TreeNodeEx(&state, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_Leaf, "<c=#F004>[<c=#F00F>%u</c>] Transition missing</c>", missingTransitionID))
                        I::TreePop();
                    I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, Conversation::COMPLETENESS_PRESUMABLY_MISSING / (float)Conversation::COMPLETENESS_COMPLETE)));
                }
                nextExpectedTransitionID = transition.TransitionID + 1;
            }

            auto [string, status] = GetString(transition.TextID);
            std::string text = string ? to_utf8(*string).c_str() : "";
            replace_all(text, "\r", R"(<c=#F00>\r</c>)");
            replace_all(text, "\n", R"(<c=#F00>\n</c>)");

            bool const transitionOpen = stateOpen && [&]
            {
                I::SetNextItemAllowOverlap();
                bool const open = I::TreeNodeEx(&transition,
                    ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | (transition.Targets.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_DefaultOpen),
                    isScriptedStartState ? "" : "<c=#4>[<c=#FFFF>%u</c>]</c> ",
                    transition.TransitionID);

                if (scoped::ItemTooltip())
                {
                    auto const drawImpl = [](char const* name, uint32 value, std::optional<uint32> def = { }, bool invert = false)
                    {
                        I::Text(!def || (value == *def ^ invert) ? "%s: %u" : "<c=#F00>%s: %u</c>", name, value);
                    };
                    #define draw(field, ...) drawImpl(#field, transition.##field, __VA_ARGS__)
                    draw(TransitionID);
                    draw(TextID, 0, true);
                    draw(CostAmount, 0);
                    draw(CostType, 2);
                    draw(CostKarma, 0);
                    draw(Diplomacy, 9);
                    draw(Unk, 3);
                    draw(Personality, 10);
                    draw(Icon);
                    draw(SkillDefDataID, 0);
                    #undef draw
                }

                if (wikiWrite && !isScriptedStartState)
                {
                    wikiBuffer.append(wikiDepth, ':');

                    std::string text = string ? to_utf8(*string).c_str() : "";
                    replace_all(text, "\r\n", "<br>");
                    replace_all(text, "\r", "<br>");
                    replace_all(text, "\n", "<br>");
                    
                    std::format_to(wiki, "{{{{dialogue icon|{}}}}} ''{}''\r\n", iconInfo[iconToIconInfoIndex[transition.Icon]].Wiki, text);
                }

                return open;
            }();
            if (!transitionOpen)
            {
                auto const id = I::GetCurrentWindow()->GetID(&transition);
                I::TreeNodeUpdateNextOpen(id, transition.Targets.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_DefaultOpen);
                I::TreePushOverrideID(id);
            }
            if (stateOpen)
            {
                I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, transition.GetCompleteness() / (float)Conversation::COMPLETENESS_COMPLETE)));

                if (drawEncounterInfo)
                {
                    I::SameLine();
                    if (transition.EncounteredTime.time_since_epoch().count())
                    {
                        if (I::Button(std::format("<c=#{}>{}</c> {}###EncounteredTime", transition.Map ? "F" : "2", ICON_FA_GLOBE, format_duration_colored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - transition.EncounteredTime))).c_str()))
                        {
                            // TODO: Open map to { transition.Map, transition.Position }
                        }
                        if (scoped::ItemTooltip())
                            I::TextUnformatted(std::format("Encountered on: {:%F %T}", std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(transition.EncounteredTime))).c_str());
                    }
                }

                if (auto const iconFileID = iconInfo[iconToIconInfoIndex[transition.Icon]].FileID)
                {
                    I::SameLine(0, 0);
                    if (scoped::WithCursorOffset(-I::GetFrameHeight() / 4, -I::GetFrameHeight() / 4))
                        DrawTexture(iconFileID, { .Size = I::GetFrameSquare() * 1.5f, .FullPreviewOnHover = false });
                    I::Dummy(I::GetFrameSquare());
                }

                I::SameLine();
                I::AlignTextToFramePadding();
                I::Text("%s%s%s",
                    drawTextID ? std::format("<c=#CCF>({})</c> ", transition.TextID).c_str() : "",
                    drawEncryptionStatus ? GetEncryptionStatusText(status) : "", 
                    text.c_str());

                if (isScriptedStartState)
                {
                    uint32 const target = transition.Targets.begin()->TargetStateID;

                    I::SameLine(0, 0);
                    if (EditingScriptedStartTransitionStateID.value_or(-1) == target)
                    {
                        auto& situation = g_config.ConversationScriptedStartSituations[ConversationID][target];
                        I::SetNextItemWidth(-FLT_MIN);
                        I::SetCursorPosX(I::GetCursorPosX() - I::GetStyle().FramePadding.x);
                        if (std::exchange(EditingScriptedStartTransitionFocus, false))
                            I::SetKeyboardFocusHere();
                        if (I::InputText("##InputSituation", &situation, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            if (situation.empty())
                                g_config.ConversationScriptedStartSituations[ConversationID].erase(target);
                            if (g_config.ConversationScriptedStartSituations[ConversationID].empty())
                                g_config.ConversationScriptedStartSituations.erase(ConversationID);
                            EditingScriptedStartTransitionStateID.reset();
                        }
                    }
                    else
                    {
                        char const* situation = "Always";
                        if (auto const itr = g_config.ConversationScriptedStartSituations.find(ConversationID); itr != g_config.ConversationScriptedStartSituations.end())
                            if (auto const itr2 = itr->second.find(target); itr2 != itr->second.end())
                                situation = itr2->second.c_str();
                        I::Text("<c=#4>%s</c>", situation);
                        I::SameLine();
                        if (I::Button(ICON_FA_PENCIL))
                        {
                            EditingScriptedStartTransitionStateID.emplace(target);
                            EditingScriptedStartTransitionFocus = true;
                        }
                    }
                }
            }

            for (auto const& target : transition.Targets)
            {
                if (transitionOpen && target.Flags)
                    if (I::TreeNodeEx("Transition Target Flags", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_Leaf, "<c=#F00>Transition Target Flags: %u</c>", target.Flags))
                        I::TreePop();

                for (auto const& state : conversation.States | std::views::filter([&target](Conversation::State const& state) { return state.StateID == target.TargetStateID; }))
                    drawState(transitionOpen, state, visitedStates, startingSpeakerNameTextID, drawState);
            }
            if (wikiWrite && transitionOpen && transition.Targets.empty())
            {
                wikiBuffer.append(wikiDepth, ':');
                std::format_to(wiki, "{{{{section-stub|npc|missing response dialogue}}}}\r\n");
            }

            I::TreePop();
        }
        I::TreePop();
    };
    
    std::set<uint32> visitedStates;
    uint32 nextExpectedStateID = 0;
    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, { I::GetStyle().FramePadding.x, 0 }))
    if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, { I::GetStyle().ItemSpacing.x, 0 })) // TODO: Remove after table
    if (scoped::WithStyleVar(ImGuiStyleVar_IndentSpacing, 16))
    for (auto const& state : conversation.States)
    {
        for (auto missingStateID = nextExpectedStateID; missingStateID < state.StateID; ++missingStateID)
        {
            if (I::TreeNodeEx(&state, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_Leaf, "<c=#F004>[<c=#F00F>%u</c>] State missing</c>", missingStateID))
                I::TreePop();
            I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, Conversation::COMPLETENESS_PRESUMABLY_MISSING / (float)Conversation::COMPLETENESS_COMPLETE)));
        }
        nextExpectedStateID = state.StateID + 1;

        uint32 startingSpeakerNameTextID = 0;
        drawState(true, state, visitedStates, startingSpeakerNameTextID, drawState);
        I::SeparatorEx(ImGuiSeparatorFlags_Horizontal | ImGuiSeparatorFlags_SpanAllColumns);
        //if (wikiWrite && !wikiBuffer.empty())
        //    wikiBuffer.append("\r\n");
    }

    if (wikiWrite && !wikiBuffer.empty())
    {
        wikiBuffer.append(1, '\0');
        I::SetClipboardText(wikiBuffer.c_str());
    }
}

struct UI::EventViewer : Viewer
{
    EventID EventID;
    std::stack<::EventID> HistoryPrev;
    std::stack<::EventID> HistoryNext;

    struct Cache
    {
        struct Data
        {
            uint32 SelectedVariant = 0;
            float Height = 0;

            float GetAndResetHeight() { return std::exchange(Height, 0); }
            void StoreHeight()
            {
                if (I::GetCurrentWindow()->DC.CursorMaxPos.y - I::GetCurrentTable()->RowPosY1 <= 5.0f)
                    I::Dummy({ 0, 10 });
                Height = I::GetCurrentWindow()->DC.CursorMaxPos.y - I::GetCurrentTable()->RowPosY1;
            }
        };
        Data Event;
        std::vector<Data> Objectives;
    };
    std::map<::EventID, Cache> Cache;
    std::optional<std::pair<::EventID, std::optional<uint32>>> Selected;
    static inline bool Invert = false;
    static inline bool InDungeonMap = false;
    static inline float PreviewProgress = 0.75f;
    struct DrawProgressParams
    {
        uint32 DisplayedAsProgressBar;
        bool DefendDefault = false;
        uint32 Invert = false;
        uint32 InvertProgress = false;
        uint32 AgentName = 0;
        std::wstring_view AgentNameLiteral;
        uint32 AgentVerb = 0;
        uint32 Text = 0;
        uint32 TextDefault = 0;
        uint32 TextDefaultInverted = TextDefault;
        std::optional<uint32> WarningTime;
        uint32 TargetCount = 100;
        uint32 Count = (uint32)(TargetCount * PreviewProgress);
        float Progress = TargetCount ? (float)Count / (float)TargetCount : PreviewProgress;
        uint32 Icon = 0;
        uint32 IconInverted = Icon;
        uint32 IconFileID = 0;
        bool IconRight = true;
        bool HideIcon = false;
        std::optional<uint32> ProgressBarColor;
        ContentObject const* ProgressBarStyleDef = nullptr;
        uint32 FormatSex = 2;
    };

    EventViewer(uint32 id, bool newTab, ::EventID eventID) : Viewer(id, newTab), EventID(eventID) { }

    std::string Title() override
    {
        std::shared_lock _(eventsLock);
        return std::format("<c=#4>Event </c>{}", events.at(EventID).Title());
    }
    void Draw() override;

    void DrawEvent(::EventID eventID);
    void DrawObjective(Event::Objective const& objective, Cache::Data& cache, uint32 index, uint32* variant = nullptr, uint32 const* variantCount = nullptr);
};
void UI::EventViewer::Draw()
{
    std::shared_lock _(eventsLock);

    if (scoped::Child("###EventViewer-Table", { 400, 0 }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX))
    {
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 2)))
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
        if (scoped::Table("Event", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableColumnFlags_WidthStretch))
        {
            I::TableSetupColumn("Variant", ImGuiTableColumnFlags_IndentDisable);
            I::TableSetupColumn("Display", ImGuiTableColumnFlags_IndentEnable | ImGuiTableColumnFlags_WidthFixed, 298.0f);
            I::TableSetupScrollFreeze(0, 1);
            I::TableHeadersRow();

            DrawEvent(EventID);
        }
    }
    if (I::SameLine(); scoped::Child("###EventViewer-Details", { }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Border))
    {
        I::Checkbox("Enemy Perspective", &Invert);
        I::SameLine();
        I::Checkbox("In Dungeon Map", &InDungeonMap);
        I::SetNextItemWidth(-FLT_MIN);
        int progress = PreviewProgress * 100;
        I::SliderInt("##Progress", &progress, 0, 100, "Progress: %u%%");
        PreviewProgress = progress / 100.0f;

        I::Separator();

        if (Selected)
        {
            auto const eventID = Selected->first;
            auto const& event = events.at(eventID);
            auto& cache = Cache[eventID];
            if (Selected->second)
            {
                if (!event.Objectives.empty())
                {
                    auto const objectiveIndex = *Selected->second;
                    auto variants = event.Objectives | std::views::filter([objectiveIndex](Event::Objective const& objective) { return objective.EventObjectiveIndex == objectiveIndex; });
                    auto const& objective = eventID.UID ? *std::next(variants.begin(), cache.Objectives.at(objectiveIndex).SelectedVariant) : *std::next(event.Objectives.begin(), objectiveIndex);
                    I::PushItemWidth(-FLT_MIN);
                    I::InputTextReadOnly("Map", std::format("{}", objective.Map)); DrawContentButton(GetContentObjectByDataID(Content::MapDef, objective.Map), &objective.Map);
                    I::InputTextReadOnly("Event UID", std::format("{}", objective.EventUID));
                    I::InputTextReadOnly("Event Objective Index", std::format("{}", objective.EventObjectiveIndex));
                    if (auto const itrType = g_config.SharedEnums.find("ObjectiveType"); itrType != g_config.SharedEnums.end())
                    {
                        TypeInfo::Symbol dummy { .Enum = TypeInfo::Enum {.Name = "ObjectiveType" } };
                        I::AlignTextToFramePadding(); I::TextUnformatted("Type:"); I::SameLine(); Symbols::GetByName("uint32")->Draw((byte const*)&objective.Type, dummy);
                    }
                    else
                        I::InputTextReadOnly("Type", std::format("{}", objective.Type));
                    I::InputTextReadOnly("Flags", std::format("0x{:X}", objective.Flags));
                    I::InputTextReadOnly("TargetCount", std::format("{}", objective.TargetCount));
                    I::InputTextReadOnly("ProgressBarStyle", std::format("{}", objective.ProgressBarStyle)); DrawContentButton(GetContentObjectByGUID(objective.ProgressBarStyle), &objective.ProgressBarStyle);

                    TypeInfo::Symbol dummy;
                    I::AlignTextToFramePadding(); I::TextUnformatted("Text:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&objective.TextID, dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("AgentName:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&objective.AgentNameTextID, dummy);

                    I::InputTextReadOnly("ExtraInt", std::format("{}", objective.ExtraInt));
                    I::InputTextReadOnly("ExtraInt2", std::format("{}", objective.ExtraInt2));
                    I::InputTextReadOnly("ExtraGUID", std::format("{}", objective.ExtraGUID)); DrawContentButton(GetContentObjectByGUID(objective.ExtraGUID), &objective.ExtraGUID);
                    I::InputTextReadOnly("ExtraGUID2", std::format("{}", objective.ExtraGUID2)); DrawContentButton(GetContentObjectByGUID(objective.ExtraGUID2), &objective.ExtraGUID2);
                    I::PopItemWidth();
                    if (!objective.ExtraBlob.empty())
                    {
                        auto _ = scoped_seh_exception_handler::Create();
                        I::TextUnformatted("ExtraBlob:");
                        HexViewerOptions options;
                        DrawHexViewer(objective.ExtraBlob, options);
                    }
                }
            }
            else
            {
                if (!event.States.empty())
                {
                    auto const& state = *std::next(event.States.begin(), cache.Event.SelectedVariant);
                    I::PushItemWidth(-FLT_MIN);
                    I::InputTextReadOnly("Map", std::format("{}", state.Map)); DrawContentButton(GetContentObjectByDataID(Content::MapDef, state.Map), &state.Map);
                    I::InputTextReadOnly("Event UID", std::format("{}", state.UID));
                    I::InputTextReadOnly("Level", std::format("{}", state.Level));
                    I::InputTextReadOnly("Flags (client)", std::format("0x{:X}\n{}", std::to_underlying(state.FlagsClient), magic_enum::enum_flags_name(state.FlagsClient, '\n')), ImGuiInputTextFlags_Multiline);
                    I::InputTextReadOnly("Flags (server)", std::format("0x{:X}\n{}", std::to_underlying(state.FlagsServer), magic_enum::enum_flags_name(state.FlagsServer, '\n')), ImGuiInputTextFlags_Multiline);
                    I::InputTextReadOnly("AudioEffect", std::format("{}", state.AudioEffect)); DrawContentButton(GetContentObjectByGUID(state.AudioEffect), &state.AudioEffect);
                    I::InputTextReadOnly("A", std::format("{}", state.A));

                    TypeInfo::Symbol dummy;
                    I::AlignTextToFramePadding(); I::TextUnformatted("Title:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.TitleTextID, dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str1%:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.TitleParameterTextID[0], dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str2%:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.TitleParameterTextID[1], dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str3%:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.TitleParameterTextID[2], dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str4%:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.TitleParameterTextID[3], dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str5%:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.TitleParameterTextID[4], dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str6%:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.TitleParameterTextID[5], dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("Description:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.DescriptionTextID, dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("MetaText:"); I::SameLine(); Symbols::GetByName("StringID")->Draw((byte const*)&state.MetaTextTextID, dummy);
                    I::AlignTextToFramePadding(); I::TextUnformatted("Icon:"); I::SameLine(); Symbols::GetByName("FileID")->Draw((byte const*)&state.FileIconID, dummy);
                    I::PopItemWidth();
                    DrawTexture(state.FileIconID);
                }
            }
        }
    }
}
void UI::EventViewer::DrawEvent(::EventID eventID)
{
    auto& cache = Cache[eventID];
    auto const& event = events.at(eventID);
    scoped::WithID(&event);

    float indent = 0;
    if (!event.States.empty())
    {
        auto const& state = *std::next(event.States.begin(), cache.Event.SelectedVariant);
        if (!state.IsMetaEvent())
            I::Indent(indent = 32 + 5);

        I::TableNextRow();

        I::TableNextColumn();
        if (auto const max = std::max(0, (int)event.States.size() - 1))
        {
            I::SetNextItemWidth(-FLT_MIN);
            I::SliderInt("##Variant", (int*)&cache.Event.SelectedVariant, 0, max, std::format("{} / {}", cache.Event.SelectedVariant + 1, max + 1).c_str());
        }

        I::GetCurrentWindow()->DC.PrevLineTextBaseOffset = 0.0f;
        I::TableNextColumn();
        auto const maxPos = I::GetCurrentWindow()->DC.CursorMaxPos;
        if (scoped::WithCursorOffset(0, 0))
        if (bool const selected = Selected && Selected->first == eventID && !Selected->second; I::Selectable("##Selected", selected, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_NoPadWithHalfSpacing, { 0, cache.Event.GetAndResetHeight() }))
        {
            if (selected)
                Selected.reset();
            else
                Selected.emplace(eventID, std::nullopt);
        }
        I::GetCurrentWindow()->DC.CursorMaxPos = maxPos;

        if (!state.IsMetaEvent())
            if (scoped::WithCursorOffset(-32 - 5, 0))
                DrawTexture(state.FileIconID ? state.FileIconID : 102388, { .Size = { 32, 32 }, .ReserveSpace = true });

        if (scoped::Table("Title", 3, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Hideable, { -FLT_MIN, 0 }))
        if (scoped::Font(g_ui.Fonts.GameHeading))
        {
            I::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupColumn("Padding", ImGuiTableColumnFlags_WidthFixed, 5);
            I::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetColumnEnabled(1, !state.IsMetaEvent());
            I::TableSetColumnEnabled(2, !state.IsMetaEvent());

            I::TableNextColumn();
            if (scoped::WithColorVar(ImGuiCol_Text, state.IsMetaEvent() ? IM_COL32(0xFF, 0xAB, 0x22, 0xFF) : IM_COL32(0xFF, 0x89, 0x44, 0xFF)))
            {
                auto title = FormatString(state.TitleTextID,
                    TEXTPARAM_STR1_CODED, state.TitleParameterTextID[0],
                    TEXTPARAM_STR2_CODED, state.TitleParameterTextID[1],
                    TEXTPARAM_STR3_CODED, state.TitleParameterTextID[2],
                    TEXTPARAM_STR4_CODED, state.TitleParameterTextID[3],
                    TEXTPARAM_STR5_CODED, state.TitleParameterTextID[4],
                    TEXTPARAM_STR6_CODED, state.TitleParameterTextID[5]);
                if (state.IsGroupEvent() && !state.IsMetaEvent())
                    title = FormatString(47464, TEXTPARAM_STR1_LITERAL, title);
                //if (state.IsMetaEvent())
                //    title = FormatString(49676, TEXTPARAM_STR1_LITERAL, title);
                I::TextWrapped("<b>%s</b>", to_utf8(title).c_str());
            }

            I::TableNextColumn();

            I::TableNextColumn();
            I::Text("<b>%s</b>", to_utf8(FormatString(301769, TEXTPARAM_NUM1, state.Level)).c_str());
        }

        if (state.MetaTextTextID)
            if (scoped::Font(g_ui.Fonts.GameText))
                I::TextWrapped("%s", to_utf8(FormatString(state.MetaTextTextID)).c_str());

        cache.Event.StoreHeight();
    }

    if (eventID.UID)
    {
        uint32 const objectiveIndexCount = event.Objectives.empty() ? 0 : std::ranges::max(event.Objectives, { }, &Event::Objective::EventObjectiveIndex).EventObjectiveIndex + 1;
        cache.Objectives.resize(std::max<size_t>(cache.Objectives.size(), objectiveIndexCount));
        for (uint32 objectiveIndex = 0; objectiveIndex < objectiveIndexCount; ++objectiveIndex)
        {
            auto variants = event.Objectives | std::views::filter([objectiveIndex](Event::Objective const& objective) { return objective.EventObjectiveIndex == objectiveIndex; });
            uint32 variantCount = std::ranges::count(event.Objectives, objectiveIndex, &Event::Objective::EventObjectiveIndex);
            auto& objectiveCache = cache.Objectives.at(objectiveIndex);
            DrawObjective(*std::next(variants.begin(), objectiveCache.SelectedVariant), objectiveCache, objectiveIndex, &objectiveCache.SelectedVariant, &variantCount);
        }
    }
    else
    {
        cache.Objectives.resize(event.Objectives.size());
        for (auto const& [objectiveIndex, objective] : event.Objectives | std::views::enumerate)
            DrawObjective(objective, cache.Objectives.at(objectiveIndex), objectiveIndex);
    }

    if (indent)
        I::Unindent(indent);
}
void UI::EventViewer::DrawObjective(Event::Objective const& objective, Cache::Data& cache, uint32 index, uint32* variant, uint32 const* variantCount)
{
    struct Params
    {
        Event::Objective const& Objective;
        EventViewer& Viewer;
        Cache::Data& Cache;
        void DrawProgress(DrawProgressParams const& params) const
        {
            bool const inverted = (bool)params.Invert ^ Invert;
            bool const defend = params.DefendDefault ^ inverted;
            float const progress = params.InvertProgress ? 1.0f - params.Progress : params.Progress;
            uint32 const count = params.InvertProgress ? params.TargetCount - params.Count : params.Count;
            if (params.DisplayedAsProgressBar)
            {
                uint32 const icons[] { 0, params.IconFileID, 156955, InDungeonMap ? 102448u : 102393, 156954, InDungeonMap ? 102542u : 156951, 102391, InDungeonMap ? 102593u : 156952, 102392, 102393, 156953 };
                auto const icon = icons[inverted ? params.IconInverted : params.Icon];
                auto const startCursor = I::GetCursorScreenPos();
                if (params.AgentName || !params.AgentNameLiteral.empty())
                    I::TextWrapped("%s", to_utf8(FormatString(49787,
                        TEXTPARAM_STR1_CODED, params.AgentName,
                        TEXTPARAM_STR1_LITERAL, params.AgentNameLiteral)).c_str());

                constexpr float progressBarHeight = 10;
                constexpr float iconPadding = 1;
                constexpr float iconSize = 24;

                if (scoped::WithColorVar(ImGuiCol_FrameBg, IM_COL32(0x20, 0x20, 0x20, 0xFF)))
                if (scoped::WithColorVar(ImGuiCol_Border, IM_COL32(0x00, 0x00, 0x00, 0xFF)))
                if (scoped::WithColorVar(ImGuiCol_PlotHistogram, params.ProgressBarColor ? *params.ProgressBarColor : IM_COL32(0xEF, 0x79, 0x24, 0xFF)))
                if (scoped::WithStyleVar(ImGuiStyleVar_FrameRounding, 1))
                if (params.HideIcon)
                {
                    I::ProgressBar(progress, { -FLT_MIN, progressBarHeight }, "");
                }
                else if (params.IconRight)
                {
                    I::ProgressBar(progress, { -(iconSize + iconPadding), progressBarHeight }, "");
                    auto const cursor = I::GetCursorScreenPos();
                    auto const contentsHeight = cursor.y - startCursor.y;
                    I::SameLine(0, iconPadding);
                    if (scoped::WithCursorOffset(0, -(contentsHeight - progressBarHeight) + (contentsHeight - iconSize) / 2))
                        DrawTexture(icon, { .Size = { iconSize, iconSize }, .ReserveSpace = true });
                    //I::NewLine();
                    I::SetCursorScreenPos(cursor);
                }
                else
                {
                    DrawTexture(icon, { .Size = { iconSize, iconSize }, .ReserveSpace = true });
                    auto const cursor = I::GetCursorScreenPos();
                    I::SameLine(0, iconPadding);
                    if (scoped::WithCursorOffset(0, (iconSize - progressBarHeight) / 2))
                        I::ProgressBar(progress, { -FLT_MIN, progressBarHeight }, "");
                    //I::NewLine();
                    I::SetCursorScreenPos(cursor);
                }
            }
            else
            {
                std::wstring time;
                uint32 color = 0xFFFFFFFF;
                if (params.WarningTime)
                {
                    time = std::vformat(params.TargetCount >= 60 * 60 ? L"{:%H:%M:%S}" : params.TargetCount >= 60 ? L"{:%M:%S}" : L"{:%S}", std::make_wformat_args(std::chrono::seconds(count)));
                    time = FormatString(47773,
                        TEXTPARAM_STR1_LITERAL, defend ? L"#C5331B" : L"#BFD47A",
                        TEXTPARAM_STR2_LITERAL, std::wstring_view { time.begin() + time.starts_with(L'0'), time.end() });
                    if (*params.WarningTime && count <= *params.WarningTime)
                    {
                        float value;
                        if (params.TextDefault == 192867)
                            value = std::max(0.0f, 1.0f - std::abs(1000 - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 2000) / 1000.0f);
                        else
                            value = std::max(0.0f, 3.0f * (1000 - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 1000) / 1000.0f - 2.0f);
                        color = I::ColorLerp(0xFFFFFFFF, 0xFF0000FF, value);
                    }
                }
                if (scoped::WithColorVar(ImGuiCol_Text, color))
                    I::TextWrapped("%s", to_utf8(FormatString(params.Text ? params.Text : inverted ? params.TextDefaultInverted : params.TextDefault,
                        TEXTPARAM_NUM1, count,
                        TEXTPARAM_NUM2, params.TargetCount,
                        TEXTPARAM_STR1_CODED, params.AgentName,
                        TEXTPARAM_STR1_LITERAL, !time.empty() ? time : params.AgentNameLiteral,
                        TEXTPARAM_STR2_CODED, params.AgentVerb,
                        TEXTPARAM_STR3_LITERAL, count == params.TargetCount ? L"#FFFFFF" : defend ? L"#C5331B" : L"#BFD47A")).c_str());
            }
        }
    };
    static std::map<std::vector<std::string_view>, std::function<void(Params const&)>> drawObjectiveType
    {
        { { "BreakMorale" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = params.Objective.Flags & 0x2,
                .DefendDefault = true,
                .Invert = params.Objective.Flags & 0x1,
                .AgentName = params.Objective.AgentNameTextID,
                .Text = params.Objective.TextID,
                .TextDefault = 47834,
                .TargetCount = 100,
                .Icon = 7,
                .IconInverted = 5,
            });
        } },
        { { "CaptureLocation" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = true,
                .Invert = params.Objective.Flags & 0x1,
                .InvertProgress = params.Objective.Flags & 0x1 ^ params.Objective.Flags & 0x8 ^ (false/*owningTeam != targetTeam*/),
                .AgentName = params.Objective.TextID,
                .Icon = 4,
                .IconInverted = 2,
            });
        } },
        { { "CollectItems" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = !params.Objective.TargetCount,
                .AgentNameLiteral = FormatString(49808, TEXTPARAM_STR1_LITERAL, GetContentObjectByGUID(params.Objective.ExtraGUID)->GetDisplayName()),
                .TextDefault = params.Objective.Flags & 0x2 ? 777812u : 47250,
                .TargetCount = params.Objective.TargetCount,
                .Icon = 1,
                .IconFileID = GetContentObjectByGUID(params.Objective.ExtraGUID)->GetIcon(),
            });
            if (!(params.Objective.Flags & 0x12))
            {
                params.DrawProgress({
                    .DisplayedAsProgressBar = false,
                    .TextDefault = 48787,
                    .TargetCount = 2,
                    .Count = 1,
                });
            }
            I::Dummy({ 0, 4 });
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .AgentNameLiteral = std::format(L"{:d}:{:02d}", (int)(60 * PreviewProgress) / 60, (int)(60 * PreviewProgress) % 60),
                .Text = params.Objective.TextID,
            });
        } },
        { { "CountKillsEnemy" }, [](Params const& params)
        {
            uint32 agentNameTextID = params.Objective.AgentNameTextID;
            if (!agentNameTextID)
                for (auto const& [agentID, agentName] : params.Objective.Agents)
                    if (!agentNameTextID && agentName)
                        agentNameTextID = agentName;

            if (params.Objective.TargetCount == 1 && !(params.Objective.Flags & 0x80))
            {
                params.DrawProgress({
                    .DisplayedAsProgressBar = true,
                    .Invert = params.Objective.Flags & 0x1,
                    .InvertProgress = params.Objective.Flags & 0x800,
                    .AgentName = agentNameTextID,
                    .TargetCount = params.Objective.TargetCount,
                    .Progress = PreviewProgress,
                    .Icon = 8,
                    .IconInverted = 5,
                    //.FormatSex = GetAgentSex,
                });
            }
            else if (params.Objective.Flags & 0x40)
            {
                uint32 targetCount = params.Objective.TargetCount ? params.Objective.TargetCount : 200;
                uint32 count = targetCount * PreviewProgress;
                float progress = 0.0f;
                if (count != params.Objective.TargetCount || params.Objective.Flags & 0x8 || params.Objective.Flags & 0x1)
                    progress = (float)count / (float)targetCount;

                params.DrawProgress({
                    .DisplayedAsProgressBar = true,
                    .Invert = params.Objective.Flags & 0x1,
                    .InvertProgress = params.Objective.Flags & 0x800,
                    .AgentName = agentNameTextID,
                    .TargetCount = targetCount,
                    .Count = count,
                    .Progress = progress,
                    .Icon = 8,
                    .IconInverted = 5,
                });
            }
            else if (params.Objective.Flags & 0x4)
            {
                uint32 targetCount = params.Objective.TargetCount ? params.Objective.TargetCount : 200;
                uint32 count = targetCount * PreviewProgress;
                params.DrawProgress({
                    .DisplayedAsProgressBar = true,
                    .Invert = params.Objective.Flags & 0x1,
                    .InvertProgress = params.Objective.Flags & 0x1 ^ params.Objective.Flags & 0x800,
                    .AgentName = agentNameTextID,
                    .TargetCount = targetCount,
                    .Count = count,
                    .Progress = targetCount ? (float)count / (float)targetCount : 0.0f,
                    .Icon = 7,
                    .IconInverted = 5,
                });
            }
            else
            {
                params.DrawProgress({
                    .DisplayedAsProgressBar = false,
                    .Invert = params.Objective.Flags & 0x1,
                    .AgentName = agentNameTextID,
                    .Text = params.Objective.TextID,
                    .TextDefault = 46248,
                    .TextDefaultInverted = 48372,
                    .TargetCount = params.Objective.TargetCount,
                    //.FormatSex = TargetCount == 1 ? GetAgentSex ?? GetObjectiveSex : GetObjectiveSex,
                });
            }
        } },
        { { "Counter" }, [](Params const& params)
        {
            // TODO: Handle leaderboards
            auto const leaderboardObjectiveDef = GetContentObjectByGUID(params.Objective.ExtraGUID);
            auto const leaderboardDef = GetContentObjectByGUID(params.Objective.ExtraGUID2);

            /* 40 can overflow
             * 1 IsCodedTextOverride
             * 2 IsInverted
             * 10 IsProgressBar
             * 4 IsProgressBarInverted
             * 8 IsScaling
             */
            if (params.Objective.Flags & 0x10)
            {
                params.DrawProgress({
                    .DisplayedAsProgressBar = true,
                    .InvertProgress = params.Objective.Flags & 0x4,
                    .AgentName = params.Objective.TextID,
                    .TargetCount = params.Objective.TargetCount,
                    .Icon = 1,
                    .IconFileID = params.Objective.ExtraInt,
                    .ProgressBarStyleDef = GetContentObjectByGUID(params.Objective.ProgressBarStyle),
                });
            }
            else if (params.Objective.Flags & 0x8)
            {
                params.DrawProgress({
                    .DisplayedAsProgressBar = false,
                    .Invert = params.Objective.Flags & 0x2,
                    .AgentName = params.Objective.Flags & 0x1 ? 0 : params.Objective.TextID,
                    .Text = params.Objective.Flags & 0x1 ? params.Objective.TextID : 0,
                    .TextDefault = 48392,
                    .TargetCount = params.Objective.TargetCount,
                });
            }
            else
            {
                params.DrawProgress({
                    .DisplayedAsProgressBar = false,
                    .Invert = params.Objective.Flags & 0x2,
                    .InvertProgress = params.Objective.Flags & 0x4,
                    .AgentName = params.Objective.Flags & 0x1 ? 0 : params.Objective.TextID,
                    .Text = params.Objective.Flags & 0x1 ? params.Objective.TextID : 0,
                    .TextDefault = params.Objective.TargetCount ? 46922u : 48797,
                    .TargetCount = params.Objective.TargetCount,
                });
            }
        } },
        { { "Cull" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = params.Objective.Flags & 0x1,
                .Invert = params.Objective.Flags & 0x2,
                .InvertProgress = (params.Objective.Flags & 0x4) ^ true,
                .AgentName = params.Objective.AgentNameTextID,
                .TextDefault = 47096,
                .TargetCount = params.Objective.TargetCount,
                .Icon = 7,
                .IconInverted = 5,
            });
        } },
        { { "DefendPlacedGadget", "DefendSpawnedGadgets" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = params.Objective.TargetCount <= 1,
                .DefendDefault = true,
                .InvertProgress = params.Objective.Flags & 0x8,
                .AgentName = params.Objective.TextID ? params.Objective.TextID : !params.Objective.Agents.empty() ? std::get<1>(params.Objective.Agents.front()) : 0,
                .Text = params.Objective.Flags & 0x4 ? params.Objective.TextID : 0,
                .TextDefault = 49039,
                .TargetCount = params.Objective.TargetCount,
                .Progress = PreviewProgress,
                .Icon = params.Objective.Flags & 0x2 ? 3u : 5,
                .ProgressBarStyleDef = GetContentObjectByGUID(params.Objective.ProgressBarStyle),
            });
        } },
        { { "DestroyPlacedGadget", "DestroySpawnedGadgets" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = params.Objective.TargetCount <= 1,
                .InvertProgress = params.Objective.Flags & 0x8,
                .AgentName = params.Objective.TextID,
                .Text = params.Objective.Flags & 0x4 ? params.Objective.TextID : 0,
                .TextDefault = 46568,
                .TargetCount = params.Objective.TargetCount,
                .Progress = PreviewProgress,
                .Icon = params.Objective.Flags & 0x2 ? 3u : (/*is prop boss ? 8 : */7),
                .ProgressBarStyleDef = GetContentObjectByGUID(params.Objective.ProgressBarStyle),
            });
        } },
        { { "Escort", "Escort2" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = params.Objective.TargetCount == 1,
                .DefendDefault = true,
                .Invert = params.Objective.Flags & 0x1,
                .AgentName = params.Objective.AgentNameTextID ? params.Objective.AgentNameTextID : !params.Objective.Agents.empty() ? std::get<1>(params.Objective.Agents.front()) : 0,
                .Text = params.Objective.TextID,
                .TextDefault = 49414,
                .TextDefaultInverted = 47296,
                .TargetCount = params.Objective.TargetCount,
                .Progress = PreviewProgress,
                .Icon = 5,
                .IconInverted = 8,
                //.HideIcon = ???,
            });
        } },
        { { "EventStatus", "EventStatus2" }, [](Params const& params)
        {
            I::TextWrapped("%s", to_utf8(FormatString(params.Objective.TextID, TEXTPARAM_NUM1, 1)).c_str());
            // TODO: Flags
            bool cacheUpdated = false;
            struct Entry
            {
                uint32 EventUID;
                uint32 SortOrder;

                Entry(uint32 eventUID, uint32 sortOrder) : EventUID(eventUID), SortOrder(sortOrder) { }
                Entry(uint32 eventUID) : Entry(eventUID, eventUID) { }
            };
            std::vector<Entry> entries;
            if (params.Objective.Time >= 1744792746000)
                entries.assign_range(std::span((Entry const*)params.Objective.ExtraBlob.data(), params.Objective.ExtraBlob.size() / sizeof(Entry)));
            else
                entries.assign_range(std::span((uint32 const*)params.Objective.ExtraBlob.data(), params.Objective.ExtraBlob.size() / sizeof(uint32)));
            std::ranges::sort(entries, { }, &Entry::SortOrder);
            for (auto const& entry : entries)
            {
                if (!std::exchange(cacheUpdated, true))
                    params.Cache.StoreHeight();
                if (::EventID const eventID { params.Objective.Map, entry.EventUID }; events.contains(eventID))
                    params.Viewer.DrawEvent(eventID);
                else
                {
                    I::TableNextRow();
                    I::TableNextColumn();
                    I::TableNextColumn();
                    I::Text("<c=#F00>Unknown Event: UID %u</c>", entry.EventUID);
                }
            }
        } },
        { { "InteractWithGadget" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .AgentName = params.Objective.TextID,
                .TextDefault = params.Objective.TargetCount ? 46922u : 48797,
                .TargetCount = params.Objective.TargetCount,
            });
        } },
        { { "Intimidate" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = params.Objective.TargetCount == 1,
                .Invert = params.Objective.Flags & 0x1,
                .InvertProgress = params.Objective.TargetCount != 1 && params.Objective.Flags & 0x1,
                .AgentName = params.Objective.AgentNameTextID ? params.Objective.AgentNameTextID : !params.Objective.Agents.empty() ? std::get<1>(params.Objective.Agents.front()) : 0,
                .Text = params.Objective.TextID,
                .TextDefault = 46248,
                .TextDefaultInverted = 48372,
                .TargetCount = params.Objective.TargetCount,
                .Progress = PreviewProgress,
                .Icon = 6,
                .IconInverted = 5,
                //.FormatSex = Agent ? GetAgentSex(Agent) ?? GetObjectiveSex : GetObjectiveSex,
            });
        } },
        { { "IntimidateScaled" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .Invert = params.Objective.Flags & 0x1,
                .AgentName = params.Objective.AgentNameTextID,
                .Text = params.Objective.TextID,
                .TextDefault = 46255,
            });
        } },
        { { "Link" }, [](Params const& params)
        {
            if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
            {
                I::PushTextWrapPos(400);
                switch (params.Objective.ExtraInt)
                {
                    case 0:
                    case 1:
                    case 2:
                    case 4:
                        I::TextWrapped("%s", to_utf8(FormatString(302045)).c_str());
                        break;
                    case 3:
                        I::TextWrapped("%s", to_utf8(FormatString(302020)).c_str());
                        break;
                }
                I::PopTextWrapPos();
            }
            static std::wstring url;
            if (I::IsItemDeactivated())
            {
                switch (params.Objective.ExtraInt)
                {
                    case 0: I::OpenPopup("Link"); url = L"http://www.guildwars2.com/competitive"; break;
                    case 1: I::OpenPopup("Link"); url = L"http://www.guildwars2.com/competitive/pvp"; break;
                    case 2: I::OpenPopup("Link"); url = L"http://www.guildwars2.com/competitive/wvw"; break;
                    case 4: I::OpenPopup("Link"); url = { (wchar_t const*)params.Objective.ExtraBlob.data(), params.Objective.ExtraBlob.size() / sizeof(wchar_t) }; if (!url.contains(L"https://www.guildwars2.com/")) url.clear(); break;
                    case 3:
                        break;
                }
            }
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .Text = params.Objective.TextID,
            });
            I::SetNextWindowPos(I::GetIO().DisplaySize / 2, ImGuiCond_Appearing, { 0.5f, 0.5f });
            I::SetNextWindowSize({ 300, 0 });
            I::SetNextWindowSizeConstraints({ 300, 0 }, { 400, 10000 });
            if (scoped::PopupModal("Link", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
            {
                I::TextWrapped("%s", to_utf8(FormatString(302270)).c_str());
                if (I::Button(to_utf8(FormatString(48858)).c_str()) && !url.empty())
                    I::OpenURL(url.c_str());
                I::SameLine();
                if (I::Button(to_utf8(FormatString(49022)).c_str()))
                    I::CloseCurrentPopup();
            }
        } },
        { { "Push" }, [](Params const& params)
        {
            I::Text("%f", ((float*)params.Objective.ExtraBlob.data())[0]);
            I::Text("%f", ((float*)params.Objective.ExtraBlob.data())[1]);
            I::Text("%f", ((float*)params.Objective.ExtraBlob.data())[2]);
        } },
        { { "QuestManual" }, [](Params const& params)
        {
            
        } },
        { { "RepairGadgets", "RepairPlacedGadget" }, [](Params const& params)
        {
            for (auto const& [agentID, agentName] : params.Objective.Agents)
            {
                params.DrawProgress({
                    .DisplayedAsProgressBar = true,
                    .Invert = params.Objective.Flags & 0x1,
                    .AgentName = agentName,
                    .Progress = PreviewProgress,
                    .Icon = 10,
                    .IconInverted = 7,
                    .ProgressBarColor = PreviewProgress == 1.0f ? IM_COL32(0x00, 0xB4, 0x00, 0xFF) : IM_COL32(0xFF, 0xFF, 0xFF, 0xFF),
                });
            }
        } },
        { { "Timer" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = params.Objective.Flags & 0x4,
                .Invert = params.Objective.Flags & 0x1,
                .InvertProgress = params.Objective.Flags & 0x2,
                .AgentName = params.Objective.Flags & 0x4 ? (params.Objective.TextID ? params.Objective.TextID : 780795) : 0,
                .Text = params.Objective.TextID,
                .TextDefault = 48017,
                .TextDefaultInverted = 47639,
                .WarningTime = params.Objective.ExtraInt / 1000 / 10,
                .TargetCount = params.Objective.ExtraInt / 1000,
                .ProgressBarStyleDef = GetContentObjectByGUID(params.Objective.ProgressBarStyle),
            });
        } },
        { { "TimeRotation" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .Invert = params.Objective.Flags & 0x1,
                .Text = params.Objective.TextID,
                .TextDefault = 48017,
                .TextDefaultInverted = 47639,
                .WarningTime = params.Objective.ExtraInt,
                .TargetCount = params.Objective.ExtraInt * 10,
            });
        } },
        { { "Tripwire" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .Invert = params.Objective.Flags & 0x1,
                .AgentName = params.Objective.AgentNameTextID,
                .AgentVerb = params.Objective.TextID,
                .TextDefault = 46263,
                .TargetCount = params.Objective.TargetCount,
            });
        } },
        { { "WvwHold" }, [](Params const& params)
        {
            auto const wvwObjectiveDef = GetContentObjectByGUID(params.Objective.ExtraGUID);
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .AgentName = wvwObjectiveDef ? (uint32)(*wvwObjectiveDef)["Name"] : 0,
                .TextDefault = 48515,
            });
        } },
        { { "WvwOrbResetTimer" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .Text = params.Objective.TextID,
                .TextDefault = 192867,
                .WarningTime = 60,
                .TargetCount = 900,
            });
        } },
        { { "WvwUpgrade" }, [](Params const& params)
        {
            auto const upgradeLineDef = GetContentObjectByGUID(params.Objective.ExtraGUID);
            auto const wvwObjectiveDef = GetContentObjectByGUID(params.Objective.ExtraGUID2);
            if (upgradeLineDef && params.Objective.ExtraInt)
            {
                uint32 const tier = params.Objective.ExtraInt - 1;
                uint32 const text = *std::next((*upgradeLineDef)["Tiers->TextName"].begin(), tier);
                uint32 const timer = *std::next((*upgradeLineDef)["Tiers->Timer"].begin(), tier);
                uint32 const costSupply = *std::next((*upgradeLineDef)["Tiers->UpgradeCostSupply"].begin(), tier);
                if (PreviewProgress >= 0.5f && timer)
                {
                    uint32 remainingTime = timer * (1.0f - (PreviewProgress * 2 - 1.0f));
                    float remainingFraction = (float)remainingTime / (float)timer;
                    params.DrawProgress({
                        .DisplayedAsProgressBar = params.Objective.Flags & 0x1,
                        .AgentName = text,
                        .TextDefault = 174716,
                        .TargetCount = 100,
                        .Count = (uint32)((1.0f - remainingFraction) * 100.0f),
                    });
                }
                else
                {
                    uint32 remainingSupply = costSupply * (1.0f - PreviewProgress * 2);
                    params.DrawProgress({
                        .DisplayedAsProgressBar = params.Objective.Flags & 0x1,
                        .AgentName = text,
                        .TextDefault = 49668,
                        .TargetCount = costSupply,
                        .Count = costSupply - remainingSupply,
                    });
                }
            }
        } },
        { { "GuildUpgrade" }, [](Params const& params)
        {
            auto const guildClaimableDef = GetContentObjectByGUID(params.Objective.ExtraGUID);
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .Text = params.Objective.TextID,
                .TextDefault = 48017,
                .TextDefaultInverted = 47639,
                .WarningTime = 10,
                .TargetCount = 100,
            });
        } },
        { { "TimeSpan" }, [](Params const& params)
        {
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .Text = params.Objective.TextID,
                .TextDefault = 48017,
                .TextDefaultInverted = 47639,
                .WarningTime = params.Objective.ExtraInt,
                .TargetCount = params.Objective.ExtraInt * 100,
            });
        } },
    };

    /*
    static Event::Objective objective;
    if (static uint32 persist = 0; persist != objectivex.EventUID)
    {
        persist = objectivex.EventUID;
        objective = 
        {
            .Type = 24,
            .Flags = (uint32)(rand() % 2),
            .ExtraInt = (uint32)(1 + rand() % 2),
            .ExtraGUID = *(*std::next(g_contentTypeInfos.at(std::ranges::find_if(g_config.TypeInfo, [](auto const& pair) { return pair.second.Name == "WvwObjectiveUpgradeLineDef"; })->first)->Objects.begin(), rand() % 10))->GetGUID(),
            .ExtraGUID2 = *(*std::next(g_contentTypeInfos.at(std::ranges::find_if(g_config.TypeInfo, [](auto const& pair) { return pair.second.Name == "WvwObjectiveDef"; })->first)->Objects.begin(), rand() % 10))->GetGUID(),
        };
        objective = 
        {
            .Type = 28,
        };
        std::wstring const url = L"https://www.guildwars2.com/en/media/";
        objective =
        {
            .Map = objectivex.Map,
            .EventUID = objectivex.EventUID,
            .Type = 16,
            .TextID = 1096296,
            .ExtraInt = (uint32)(rand() % 5),
            .ExtraBlob = { std::from_range, std::span((byte const*)url.data(), url.size() * sizeof(wchar_t)) },
        };
        objective =
        {
            .Map = objectivex.Map,
            .EventUID = objectivex.EventUID,
            .Type = 21,
            .TargetCount = 69,
            .TextID = 823770,
            .AgentNameTextID = 1112544,
        };
    }
    */

    I::TableNextRow();
    scoped::WithID(index);

    ::EventID const eventID { objective.Map, objective.EventUID };

    I::TableNextColumn();
    if (variant && variantCount && *variantCount - 1)
    {
        I::SetNextItemWidth(-FLT_MIN);
        I::SliderInt("##Variant", (int*)variant, 0, *variantCount - 1, std::format("{} / {}", *variant + 1, *variantCount).c_str());
    }

    I::GetCurrentWindow()->DC.PrevLineTextBaseOffset = 0.0f;
    I::TableNextColumn();
    auto const maxPos = I::GetCurrentWindow()->DC.CursorMaxPos;
    if (scoped::WithCursorOffset(0, 0))
    if (bool const selected = Selected && Selected->first == eventID && Selected->second == index; I::Selectable("##Selected", selected, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_NoPadWithHalfSpacing, { 0, cache.GetAndResetHeight() }))
    {
        if (selected)
            Selected.reset();
        else
            Selected.emplace(eventID, index);
    }
    I::GetCurrentWindow()->DC.CursorMaxPos = maxPos;

    if (auto const itrType = g_config.SharedEnums.find("ObjectiveType"); itrType != g_config.SharedEnums.end())
        if (auto const itrValue = itrType->second.Values.find(objective.Type); itrValue != itrType->second.Values.end())
            if (auto const itrDraw = std::ranges::find_if(drawObjectiveType, [name = itrValue->second](auto const& pair) { return std::ranges::contains(pair.first, name); }); itrDraw != drawObjectiveType.end())
                if (scoped::Font(g_ui.Fonts.GameText))
                    itrDraw->second({ .Objective = objective, .Viewer = *this, .Cache = cache });

    if (!cache.Height)
        cache.StoreHeight();
}

struct UI::MapLayoutViewer : Viewer
{
    ::MapLayoutViewer MapViewer;

    MapLayoutViewer(uint32 id, bool newTab) : Viewer(id, newTab)
    {
        MapViewer.MapLayoutContinent = g_contentObjectsByGUID.at(scn::scan_value<GUID>("21742531-35A3-4763-A670-8B774B2B27AC").value());
        MapViewer.MapLayoutContinentFloor = g_contentObjectsByGUID.at(scn::scan_value<GUID>("DD8189AD-0359-4C17-AD7B-5CA5B33E52F3").value());
        MapViewer.Initialize();
    }

    std::string Title() override { return ICON_FA_GLOBE " World Map"; }
    void Draw() override
    {
        MapViewer.Draw();
    }
};

#pragma endregion
#pragma region PackFile Outline

std::string* g_writeTokensTargets;
std::string* g_writeStringsTargets;

template<template<typename PointerType> typename PackFileType, typename PointerType> struct DrawPackFileField { };
template<template<typename SizeType, typename PointerType> typename PackFileType, typename SizeType, typename PointerType> struct DrawPackFileFieldArray { };

void DrawPackFileType(byte const*& p, bool x64, pf::Layout::Type const* type, pf::Layout::Field const* parentField = nullptr);
template<template<typename PointerType> typename PackFileType>
void DrawPackFileFieldByArch(byte const*& p, pf::Layout::Field const& field, bool x64)
{
    if (x64)
        DrawPackFileField<PackFileType, int64> { }((PackFileType<int64> const*&)p, field);
    else
        DrawPackFileField<PackFileType, int32> { }((PackFileType<int32> const*&)p, field);
}
template<typename PointerType> struct DrawPackFileField<pf::GenericPtr, PointerType>
{
    void operator()(pf::GenericPtr<PointerType> const*& p, pf::Layout::Field const& field)
    {
        byte const* ep = p->get();
        ++p;

        I::SameLine();
        I::Text("<c=#4>%s*</c>", field.ElementType->Name.c_str());
        if (!ep)
        {
            I::SameLine();
            I::TextUnformatted("<c=#CCF><nullptr></c>");
            return;
        }
        I::Dummy({ 25, 0 });
        I::SameLine();
        if (scoped::Group())
            DrawPackFileType(ep, std::is_same_v<PointerType, int64>, field.ElementType, &field);
    }
};
template<typename SizeType, typename PointerType> struct DrawPackFileFieldArray<pf::GenericArray, SizeType, PointerType>
{
    void operator()(pf::GenericArray<SizeType, PointerType> const*& p, pf::Layout::Field const& field)
    {
        byte const* ep = p->data();
        uint32 const size = p->size();
        ++p;

        I::SameLine();
        I::Text("<c=#4>%s[%u]</c>", field.ElementType->Name.c_str(), size);
        if (size > 10 && (field.ElementType->Name == "byte" || field.ElementType->Name == "word" || field.ElementType->Name == "float" || field.ElementType->Name == "float3"))
        {
            I::SameLine();
            I::Text("<c=#0F0><omitted></c>");
            return;
        }
        I::Dummy({ 25, 0 });
        I::SameLine();
        if (scoped::Group())
        for (uint32 i = 0; i < size; ++i)
        {
            I::Text("[%u] ", i);
            I::SameLine();
            if (scoped::Group())
                DrawPackFileType(ep, std::is_same_v<PointerType, int64>, field.ElementType, &field);
        }
    }
};
template<typename PointerType> struct DrawPackFileField<pf::GenericDwordArray, PointerType> : DrawPackFileFieldArray<pf::GenericArray, uint32, PointerType> { };
template<typename PointerType> struct DrawPackFileField<pf::GenericWordArray, PointerType> : DrawPackFileFieldArray<pf::GenericArray, uint16, PointerType> { };
template<typename PointerType> struct DrawPackFileField<pf::GenericByteArray, PointerType> : DrawPackFileFieldArray<pf::GenericArray, byte, PointerType> { };
template<typename SizeType, typename PointerType> struct DrawPackFileFieldArray<pf::GenericPtrArray, SizeType, PointerType>
{
    void operator()(pf::GenericPtrArray<SizeType, PointerType> const*& p, pf::Layout::Field const& field)
    {
        pf::GenericPtr<PointerType> const* ep = p->data();
        uint32 const size = p->size();
        ++p;

        I::SameLine();
        I::Text("<c=#4>%s*[%u]</c>", field.ElementType->Name.c_str(), size);
        I::Dummy({ 25, 0 });
        I::SameLine();
        if (scoped::Group())
        for (uint32 i = 0; i < size; ++i)
        {
            I::Text("[%u] ", i);
            I::SameLine();
            if (scoped::Group())
                DrawPackFileField<pf::GenericPtr, PointerType> { }(ep, field);
        }
    }
};
template<typename PointerType> struct DrawPackFileField<pf::GenericDwordPtrArray, PointerType> : DrawPackFileFieldArray<pf::GenericPtrArray, uint32, PointerType> { };
template<typename PointerType> struct DrawPackFileField<pf::GenericWordPtrArray, PointerType> : DrawPackFileFieldArray<pf::GenericPtrArray, uint16, PointerType> { };
template<typename PointerType> struct DrawPackFileField<pf::GenericBytePtrArray, PointerType> : DrawPackFileFieldArray<pf::GenericPtrArray, byte, PointerType> { };
template<typename SizeType, typename PointerType> struct DrawPackFileFieldArray<pf::GenericTypedArray, SizeType, PointerType>
{
    void operator()(pf::GenericTypedArray<SizeType, PointerType> const*& p, pf::Layout::Field const& field)
    {
        byte const* ep = p->data();
        uint32 const size = p->size();
        ++p;

        std::terminate(); // TODO: Not yet implemented
    }
};
template<typename PointerType> struct DrawPackFileField<pf::GenericDwordTypedArray, PointerType> : DrawPackFileFieldArray<pf::GenericTypedArray, uint32, PointerType> { };
template<typename PointerType> struct DrawPackFileField<pf::GenericWordTypedArray, PointerType> : DrawPackFileFieldArray<pf::GenericTypedArray, uint16, PointerType> { };
template<typename PointerType> struct DrawPackFileField<pf::GenericByteTypedArray, PointerType> : DrawPackFileFieldArray<pf::GenericTypedArray, byte, PointerType> { };
template<typename PointerType> struct DrawPackFileField<pf::FileNameBase, PointerType>
{
    void operator()(pf::FileNameBase<PointerType> const*& p, pf::Layout::Field const& field)
    {
        auto const fileID = p->GetFileID();
        scoped::WithID(p);
        ++p;

        I::SameLine();
        I::Button(std::format("File <{}>", fileID).c_str());
        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
            if (auto* file = g_archives.GetFileEntry(fileID))
                g_ui.OpenFile(*file, button & ImGuiButtonFlags_MouseButtonMiddle);
        if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
            DrawTexture(fileID);
    }
};
template<typename PointerType> struct DrawPackFileField<pf::String, PointerType>
{
    void operator()(pf::String<PointerType> const*& p, pf::Layout::Field const& field)
    {
        I::SameLine();
        if (auto const* string = p->data())
        {
            I::TextUnformatted(string);
            if (g_writeStringsTargets)
                *g_writeStringsTargets += std::format("{}\n", string);
        }
        else
            I::TextUnformatted("<c=#CCF><nullptr></c>");
        ++p;
    }
};
template<typename PointerType> struct DrawPackFileField<pf::WString, PointerType>
{
    void operator()(pf::WString<PointerType> const*& p, pf::Layout::Field const& field)
    {
        I::SameLine();
        if (auto const* string = p->data())
        {
            I::TextUnformatted(to_utf8(string).c_str());
            if (g_writeStringsTargets)
                *g_writeStringsTargets += std::format("{}\n", to_utf8(string));
        }
        else
            I::TextUnformatted("<c=#CCF><nullptr></c>");
        ++p;
    }
};
template<typename PointerType> struct DrawPackFileField<pf::Variant, PointerType>
{
    void operator()(pf::Variant<PointerType> const*& p, pf::Layout::Field const& field)
    {
        byte const* ep = p->data();
        auto const& type = field.VariantElementTypes.at(p->index());
        ++p;

        I::SameLine();
        I::TextUnformatted(std::format("<c=#4>Variant<{}></c>", std::string { std::from_range, field.VariantElementTypes | std::views::transform([](pf::Layout::Type const* type) { return type->Name; }) | std::views::join_with(',') }).c_str());
        I::Dummy({ 25, 0 });
        I::SameLine();
        I::Text("[%s] ", type->Name.c_str());
        I::SameLine();
        if (scoped::Group())
            DrawPackFileType(ep, std::is_same_v<PointerType, int64>, type, &field);
    }
};
void DrawPackFileFieldValue(byte const*& p, bool x64, pf::Layout::Field const& field, pf::Layout::Field const* parentField = nullptr)
{
    // TODO: field.RealType
    switch (field.UnderlyingType)
    {
        using enum pf::Layout::UnderlyingTypes;
        case Byte:
            I::SameLine();
            I::Text("<c=#4>byte</c> %u", (uint32)*p++);
            break;
        case Byte3:
            I::SameLine();
            I::Text("<c=#4>byte3</c> (%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u)", (uint32)*p++);
            break;
        case Byte4:
            I::SameLine();
            I::Text("<c=#4>byte4</c> (%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u)", (uint32)*p++);
            break;
        case Byte16:
            I::SameLine();
            I::Text("<c=#4>byte16</c> (%u, ", (uint32)*p++);
            for (uint32 i = 0; i < 14; ++i)
            {
                I::SameLine(0, 0);
                I::Text("%u, ", (uint32)*p++);
            }
            I::SameLine(0, 0);
            I::Text("%u)", (uint32)*p++);
            break;
        case Word:
            I::SameLine();
            I::Text("<c=#4>word</c> %d", (int32)*((int16 const*&)p)++);
            break;
        case Word3:
            I::SameLine();
            I::Text("<c=#4>word3</c> (%d, ", (int32)*((int16 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d, ", (int32)*((int16 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d)", (int32)*((int16 const*&)p)++);
            break;
        case Dword:
        case DwordID:
            I::SameLine();
            if (field.RealType == pf::Layout::RealTypes::Token || parentField && parentField->RealType == pf::Layout::RealTypes::Token)
            {
                auto token = ((pf::Token32 const*)p)->GetString();
                I::Text("<c=#4>token32</c> %s", token.data());
                if (g_writeTokensTargets && *token.data())
                    *g_writeTokensTargets += std::format("{}\n", token.data());
            }
            else
            {
                I::Text("<c=#4>dword</c> %d", *(int32 const*)p);
                I::SameLine(0, 0);
                auto token = ((pf::Token32 const*)p)->GetString();
                I::Text("<c=#4> or token32</c> %s", token.data());
                if (g_writeTokensTargets && *token.data())
                    *g_writeTokensTargets += std::format("{}\n", token.data());
            }
            ++(int32 const*&)p;
            break;
        case Dword2:
            I::SameLine();
            I::Text("<c=#4>dword2</c> (%d, ", *((int32 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d)", *((int32 const*&)p)++);
            break;
        case Dword4:
            I::SameLine();
            I::Text("<c=#4>dword4</c> (%d, ", *((int32 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d, ", *((int32 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d, ", *((int32 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d)", *((int32 const*&)p)++);
            break;
        case Qword:
        case QwordID:
            I::SameLine();
            if (field.RealType == pf::Layout::RealTypes::Token || parentField && parentField->RealType == pf::Layout::RealTypes::Token)
            {
                auto token = ((pf::Token64 const*)p)->GetString();
                I::Text("<c=#4>token64</c> %s", token.data());
                if (g_writeTokensTargets && *token.data())
                    *g_writeTokensTargets += std::format("{}\n", token.data());
            }
            else
            {
                I::TextUnformatted(std::format("<c=#4>qword</c> {}", *(int64 const*)p).c_str());
                I::SameLine(0, 0);
                auto token = ((pf::Token64 const*)p)->GetString();
                I::Text("<c=#4> or token64</c> %s", token.data());
                if (g_writeTokensTargets && *token.data())
                    *g_writeTokensTargets += std::format("{}\n", token.data());
            }
            ++(int64 const*&)p;
            break;
        case Float:
            I::SameLine();
            I::Text("<c=#4>float</c> %f", *((float const*&)p)++);
            break;
        case Float2:
            I::SameLine();
            I::Text("<c=#4>float2</c> (%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((float const*&)p)++);
            break;
        case Float3:
            I::SameLine();
            I::Text("<c=#4>float3</c> (%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((float const*&)p)++);
            break;
        case Float4:
            I::SameLine();
            I::Text("<c=#4>float4</c> (%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((float const*&)p)++);
            break;
        case Double:
            I::SameLine();
            I::Text("<c=#4>double</c> %f", *((double const*&)p)++);
            break;
        case Double2:
            I::SameLine();
            I::Text("<c=#4>double2</c> (%f, ", *((double const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((double const*&)p)++);
            break;
        case Double3:
            I::SameLine();
            I::Text("<c=#4>double3</c> (%f, ", *((double const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f, ", *((double const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((double const*&)p)++);
            break;
        case InlineArray:
            I::SameLine();
            I::Text("<c=#4>%s[%u]</c>", field.ElementType->Name.c_str(), field.ArraySize);
            I::Dummy({ 25, 0 });
            I::SameLine();
            if (scoped::Group())
            for (uint32 i = 0; i < field.ArraySize; ++i)
            {
                I::Text("[%u] ", i);
                I::SameLine();
                if (scoped::Group())
                    DrawPackFileType(p, x64, field.ElementType, &field);
            }
            break;
        case DwordArray: DrawPackFileFieldByArch<pf::GenericDwordArray>(p, field, x64); break;
        case WordArray: DrawPackFileFieldByArch<pf::GenericWordArray>(p, field, x64); break;
        case ByteArray: DrawPackFileFieldByArch<pf::GenericByteArray>(p, field, x64); break;
        case DwordPtrArray: DrawPackFileFieldByArch<pf::GenericDwordPtrArray>(p, field, x64); break;
        case WordPtrArray: DrawPackFileFieldByArch<pf::GenericWordPtrArray>(p, field, x64); break;
        case BytePtrArray: DrawPackFileFieldByArch<pf::GenericBytePtrArray>(p, field, x64); break;
        case DwordTypedArray: DrawPackFileFieldByArch<pf::GenericDwordTypedArray>(p, field, x64); break;
        case WordTypedArray: DrawPackFileFieldByArch<pf::GenericWordTypedArray>(p, field, x64); break;
        case ByteTypedArray: DrawPackFileFieldByArch<pf::GenericByteTypedArray>(p, field, x64); break;
        case FileName:
        case FileName2: DrawPackFileFieldByArch<pf::FileNameBase>(p, field, x64); break;
        case Ptr: DrawPackFileFieldByArch<pf::GenericPtr>(p, field, x64); break;
        case String: DrawPackFileFieldByArch<pf::String>(p, field, x64); break;
        case WString: DrawPackFileFieldByArch<pf::WString>(p, field, x64); break;
        case Variant: DrawPackFileFieldByArch<pf::Variant>(p, field, x64); break;
        case InlineStruct:
        case InlineStruct2:
            I::Dummy({ 25, 0 });
            I::SameLine();
            if (scoped::Group())
                DrawPackFileType(p, x64, field.ElementType, &field);
            break;
        default:
            I::SameLine();
            I::Text("<c=#F00>Unhandled type %u</c>", (uint32)field.UnderlyingType);
            break;
    }
}
void DrawPackFileType(byte const*& p, bool x64, pf::Layout::Type const* type, pf::Layout::Field const* parentField)
{
    for (auto const& field : type->Fields)
    {
        I::Text("<c=#8>%s = </c>", field.Name.c_str());
        DrawPackFileFieldValue(p, x64, field, parentField);
    }
}

#pragma endregion
#pragma region PackFileChunkPreview

struct PackFileChunkPreviewBase
{
    virtual ~PackFileChunkPreviewBase() = default;
    virtual void DrawPreview(pf::Layout::Traversal::QueryChunk const& chunk) { }
};
static auto& GetPackFileChunkPreviewRegistry()
{
    static std::unordered_map<fcc, std::function<PackFileChunkPreviewBase*()>> instance { };
    return instance;
}
template<fcc FourCC>
struct RegisterPackFileChunkPreview
{
    static bool Register();
    inline static bool Registered = Register();
};

template<fcc FourCC>
struct PackFileChunkPreview : PackFileChunkPreviewBase { };

template<>
struct PackFileChunkPreview<fcc::PGTB> : RegisterPackFileChunkPreview<fcc::PGTB>, PackFileChunkPreviewBase
{
    std::optional<ImVec2> ViewportOffset { };

    void DrawPreview(pf::Layout::Traversal::QueryChunk const& chunk) override
    {
#pragma pack(push, 4)
        static struct PixelBuffer
        {
            ImVec4 Channels { 1, 1, 1, 1 };
            int AlphaMode;
            float Padding[3];
        } pixelBuffer { };
#pragma pack(pop)
        static_assert(sizeof(PixelBuffer) % 16 == 0); // Shader requirement to buffers
        if (bool channel = pixelBuffer.Channels.x == 1; I::CheckboxButton("<c=#F00>" ICON_FA_SQUARE "</c>", channel, "Show Red Channel", { I::GetFrameHeight(), I::GetFrameHeight() }))
            pixelBuffer.Channels.x = channel ? 1 : 0;
        I::SameLine(0, 0);
        if (bool channel = pixelBuffer.Channels.y == 1; I::CheckboxButton("<c=#0F0>" ICON_FA_SQUARE "</c>", channel, "Show Green Channel", { I::GetFrameHeight(), I::GetFrameHeight() }))
            pixelBuffer.Channels.y = channel ? 1 : 0;
        I::SameLine(0, 0);
        if (bool channel = pixelBuffer.Channels.z == 1; I::CheckboxButton("<c=#00F>" ICON_FA_SQUARE "</c>", channel, "Show Blue Channel", { I::GetFrameHeight(), I::GetFrameHeight() }))
            pixelBuffer.Channels.z = channel ? 1 : 0;
        I::SameLine();
        I::AlignTextToFramePadding();
        I::TextUnformatted(ICON_FA_GAME_BOARD_SIMPLE);
        I::SameLine(0, 0);
        if (I::Button(std::format("<c=#{}>" ICON_FA_VIRUS "</c><c=#{}>" ICON_FA_SQUARE_VIRUS "</c><c=#{}>" ICON_FA_SQUARE "</c>###AlphaMode", pixelBuffer.AlphaMode == 0 ? "F" : "4", pixelBuffer.AlphaMode == 1 ? "F" : "4",             pixelBuffer.AlphaMode == 2 ? "F" : "4").c_str(), { 0, I::GetFrameHeight() }))
            pixelBuffer.AlphaMode = (pixelBuffer.AlphaMode + 1) % 3;
        static auto buffer = ImGui_ImplDX11_CreateBuffer(sizeof(PixelBuffer));
        static auto shader = ImGui_ImplDX11_CompilePixelShader(R"(
cbuffer pixelBuffer : register(b0)
{
    float4 Channels;
    int AlphaMode;
    float Padding[2];
};
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};
sampler sampler0;
Texture2D texture0;

float4 main(PS_INPUT input) : SV_Target
{
    float4 tex = texture0.Sample(sampler0, input.uv);
    float4 out_col = input.col * tex * Channels;
    if (AlphaMode == 1)
        out_col.rgba = float4(out_col.aaa, 1.0f);
    else if (AlphaMode == 2)
        out_col.a = 1.0f;
    return out_col;
}
)");

        auto const cursor = I::GetCursorScreenPos();
        auto const viewportSize = I::GetContentRegionAvail();
        ImRect const viewportScreenRect { cursor, cursor + viewportSize };
        scoped::Child("Viewport", viewportSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        I::InvisibleButton("Canvas", viewportSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

        bool const initViewportOffset = !ViewportOffset;
        if (initViewportOffset)
            ViewportOffset.emplace();
        if (I::IsItemActive() && I::IsMouseDragging(ImGuiMouseButton_Left))
        {
            float const scale = (I::GetIO().KeyShift ? 10.0f : 1.0f) * (I::GetIO().KeyCtrl ? 100.0f : 1.0f);
            *ViewportOffset -= I::GetMouseDragDelta(ImGuiMouseButton_Left) * scale;
            I::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
        *ViewportOffset = { (float)(int)ViewportOffset->x, (float)(int)ViewportOffset->y };

        struct Layer
        {
            ImVec2 StrippedDims { };
            ImRect ContentsRect { };
        };
        std::vector<Layer> layers;
        layers.reserve(chunk["layers"].GetArraySize());
        for (auto const& layerData : chunk["layers"])
        {
            auto& layer = layers.emplace_back();
            std::array<float, 2> strippedDims = layerData["strippedDims"];
            layer.StrippedDims = { strippedDims[0], strippedDims[1] };
        }

        I::GetWindowDrawList()->AddCallback([](ImDrawList const* parent_list, ImDrawCmd const* cmd)
        {
            ImGui_ImplDX11_SetPixelShader(shader);
            ImGui_ImplDX11_SetPixelShaderConstantBuffer(buffer, &pixelBuffer, sizeof(pixelBuffer));
        }, nullptr);

        for (auto const& pageData : chunk["strippedPages"])
        {
            std::array<float, 2> const coord = pageData["coord"];

            auto& layer = layers.at(pageData["layer"]);
            ImVec2 pagePos = layer.StrippedDims * ImVec2 { coord[0], coord[1] };
            if (layer.ContentsRect.Min == layer.ContentsRect.Max)
                layer.ContentsRect = { pagePos, pagePos + layer.StrippedDims };
            else
                layer.ContentsRect.Add(ImRect { pagePos, pagePos + layer.StrippedDims });
            ImVec2 drawPos = cursor - *ViewportOffset + pagePos;
            if (viewportScreenRect.Overlaps({ drawPos, drawPos + layer.StrippedDims }))
                if (scoped::WithCursorScreenPos(drawPos))
                    DrawTexture(pageData["filename"], { .Size = layer.StrippedDims, .FullPreviewOnHover = false, .AdvanceCursor = false });
        }

        I::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

        if (initViewportOffset)
            ViewportOffset = layers[0].ContentsRect.GetCenter() - viewportSize * 0.5f;
    }
};

template<fcc FourCC> bool RegisterPackFileChunkPreview<FourCC>::Register()
{
    return [] { return GetPackFileChunkPreviewRegistry().emplace(FourCC, []<typename... Args>(Args&&... args) { return new PackFileChunkPreview<FourCC>(std::forward<Args>(args)...); }).second; }();
}

#pragma endregion
#pragma region FileViewer

template<fcc FourCC>
struct UI::FileViewer : RawFileViewer { };

template<>
struct UI::FileViewer<fcc::PF5> : RegisterFileViewer<fcc::PF5>, RawFileViewer
{
    using RawFileViewer::RawFileViewer;

    std::unique_ptr<pf::PackFile> PackFile;
    std::vector<std::unique_ptr<PackFileChunkPreviewBase>> ChunkPreview;

    void Initialize() override
    {
        PackFile = File.Source.get().Archive.GetPackFile(File.ID);
    }
    void DrawOutline() override
    {
        g_writeTokensTargets = nullptr;
        if (static ImGuiID sharedScope = 3; scoped::Child(sharedScope, { }, ImGuiChildFlags_Border | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
        {
            if (scoped::TabBar("Tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_NoTabListScrollingButtons))
            {
                if (scoped::TabItem(ICON_FA_MAGNIFYING_GLASS " Search", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
                {
                    static bool resultsAsTree = false;
                    I::CheckboxButton(ICON_FA_FOLDER_TREE, resultsAsTree, "Expand Results into Trees", I::GetFrameHeight());
                    I::SameLine();
                    static std::string query;
                    I::TextUnformatted("Query:");
                    I::SameLine();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::InputText("##Query", &query);
                    if (!query.empty())
                    {
                        try
                        {
                            if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2(I::GetStyle().CellPadding.x, 0)))
                            if (scoped::Table("Results", 3))
                            {
                                I::TableSetupColumn("Chunk", ImGuiTableColumnFlags_WidthFixed);
                                I::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed);
                                I::TableSetupColumn(std::format("{}###Value", query).c_str());
                                I::TableHeadersRow();

                                uint32 i = 0;
                                for (auto const& chunk : *PackFile)
                                {
                                    std::string const fcc { (char const*)&chunk.Header.Magic, 4 };
                                    for (auto const& field : pf::Layout::Traversal::QueryPackFileFields(*PackFile, chunk, query))
                                    {
                                        auto p = field.GetPointer();
                                        I::TableNextRow();
                                        I::TableNextColumn(); I::Text("<c=#4>%s</c>", fcc.c_str());
                                        I::TableNextColumn(); I::Text("<c=#4>#</c><c=#8>%u</c>", i++);
                                        I::TableNextColumn();
                                        if (field.IsArrayIterator())
                                        {
                                            if (resultsAsTree)
                                                DrawPackFileType(p, PackFile->Header.Is64Bit, &field.GetArrayType());
                                            else
                                                DrawPackFileFieldValue(p, PackFile->Header.Is64Bit, field.GetField().ElementType->Fields.front());
                                        }
                                        else
                                            DrawPackFileFieldValue(p, PackFile->Header.Is64Bit, field.GetField());
                                    }
                                }
                            }
                        }
                        catch (std::exception const& ex)
                        {
                            I::Text("<c=#F00>Error: %s</c>", ex.what());
                        }
                        I::Dummy({ 0, 20 });
                    }
                }
                if (scoped::TabItem(ICON_FA_TEXT_SIZE " Tokens", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                {
                    static std::string tokens;
                    if (I::Button(ICON_FA_COPY " Copy All"))
                        I::SetClipboardText(tokens.c_str());
                    if (tokens.empty())
                        I::TextUnformatted("<c=#4><no tokens in the file></c>");
                    else
                        I::TextUnformatted(tokens.c_str());
                    tokens.clear();
                    g_writeTokensTargets = &tokens;
                }
                if (scoped::TabItem(ICON_FA_FONT_CASE " Strings", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                {
                    static std::string strings;
                    if (I::Button(ICON_FA_COPY " Copy All"))
                        I::SetClipboardText(strings.c_str());
                    if (strings.empty())
                        I::TextUnformatted("<c=#4><no strings in the file></c>");
                    else
                        I::TextUnformatted(strings.c_str());
                    strings.clear();
                    g_writeStringsTargets = &strings;
                }
            }
        }

        for (auto const& chunk : *PackFile)
        {
            std::string const fcc { (char const*)&chunk.Header.Magic, 4 };
            auto const* p = chunk.Data;
            I::TextUnformatted(std::format("Chunk <{}>", fcc.c_str()).c_str());
            I::Dummy({ 25, 0 });
            I::SameLine();
            if (scoped::Group())
                if (auto const itrChunk = pf::Layout::g_chunks.find(fcc); itrChunk != pf::Layout::g_chunks.end())
                    if (auto const itrChunkVersion = itrChunk->second.find(chunk.Header.Version); itrChunkVersion != itrChunk->second.end())
                        DrawPackFileType(p, PackFile->Header.Is64Bit, itrChunkVersion->second);
        }
    }
    void DrawPreview() override
    {
        uint32 index = 0;
        for (auto const& chunk : *PackFile)
        {
            if (ChunkPreview.size() <= index)
                ChunkPreview.resize(index + 1);

            auto& preview = ChunkPreview[index++];
            if (!preview)
                if (auto const itr = GetPackFileChunkPreviewRegistry().find(chunk.Header.Magic); itr != GetPackFileChunkPreviewRegistry().end())
                    preview.reset(itr->second());

            if (preview)
                preview->DrawPreview(PackFile->QueryChunk(chunk.Header.Magic));
        }
    }
};

template<> struct UI::FileViewer<fcc::PF4> : RegisterFileViewer<fcc::PF4>, FileViewer<fcc::PF5> { using FileViewer<fcc::PF5>::FileViewer; };
template<> struct UI::FileViewer<fcc::PF3> : RegisterFileViewer<fcc::PF3>, FileViewer<fcc::PF5> { using FileViewer<fcc::PF5>::FileViewer; };
template<> struct UI::FileViewer<fcc::PF2> : RegisterFileViewer<fcc::PF2>, FileViewer<fcc::PF5> { using FileViewer<fcc::PF5>::FileViewer; };
template<> struct UI::FileViewer<fcc::PF1> : RegisterFileViewer<fcc::PF1>, FileViewer<fcc::PF5> { using FileViewer<fcc::PF5>::FileViewer; };

template<fcc FourCC> bool UI::RegisterFileViewer<FourCC>::Register()
{
    return [] { return GetFileViewerRegistry().emplace(FourCC, []<typename... Args>(Args&&... args) { return new FileViewer<FourCC>(std::forward<Args>(args)...); }).second; }();
}

#pragma endregion

void UI::OpenFile(ArchiveFile const& file, bool newTab, bool historyMove)
{
    static auto init = [](uint32 id, bool newTab, ArchiveFile const& file)
    {
        RawFileViewer* result = nullptr;
        if (auto const data = file.Source.get().Archive.GetFile(file.ID); data.size() >= 4) // TODO: Refactor to avoid copying
            if (auto const itr = GetFileViewerRegistry().find(*(fcc const*)data.data()); itr != GetFileViewerRegistry().end())
                result = itr->second(id, newTab, file);
        if (!result)
            result = new RawFileViewer(id, newTab, file);
        result->Initialize();
        return result;
    };
    Defer([=]
    {
        if (I::GetIO().KeyAlt)
        {
            auto data = file.Source.get().Archive.GetFile(file.ID);
            g_ui.ExportData(data, std::format(R"(Export\{})", file.ID));
            LoadTexture(file.ID, { .DataSource = &data, .Export = true });
            return;
        }

        if (auto* currentViewer = dynamic_cast<RawFileViewer*>(m_currentViewer); currentViewer && !newTab)
        {
            if (currentViewer->File == file)
                return;

            auto const id = currentViewer->ID;
            auto historyPrev = std::move(currentViewer->HistoryPrev);
            auto historyNext = std::move(currentViewer->HistoryNext);
            if (!historyMove)
            {
                historyPrev.emplace(currentViewer->File);
                historyNext = { };
            }

            //currentViewer->~RawFileViewer();
            //new(currentViewer) RawFileViewer(id, newTab, file);

            auto const itr = std::ranges::find(m_viewers, m_currentViewer, [](auto const& ptr) { return ptr.get(); });
            itr->reset(init(id, newTab, file));
            currentViewer = dynamic_cast<RawFileViewer*>(m_currentViewer = itr->get());
            
            currentViewer->HistoryPrev = std::move(historyPrev);
            currentViewer->HistoryNext = std::move(historyNext);
        }
        else
            m_viewers.emplace_back(init(m_nextViewerID++, newTab, file));
    });
}
void UI::OpenContent(ContentObject& object, bool newTab, bool historyMove)
{
    Defer([=, &object]
    {
        if (auto* currentViewer = dynamic_cast<ContentViewer*>(m_currentViewer); currentViewer && !newTab)
        {
            if (&currentViewer->Content == &object)
                return;

            auto const id = currentViewer->ID;
            auto historyPrev = std::move(currentViewer->HistoryPrev);
            auto historyNext = std::move(currentViewer->HistoryNext);
            if (!historyMove)
            {
                historyPrev.emplace(&currentViewer->Content);
                historyNext = { };
            }
            currentViewer->~ContentViewer();
            new(currentViewer) ContentViewer(id, newTab, object);
            currentViewer->HistoryPrev = std::move(historyPrev);
            currentViewer->HistoryNext = std::move(historyNext);
        }
        else
            m_viewers.emplace_back(new ContentViewer(m_nextViewerID++, newTab, object));
    });
}
void UI::OpenConversation(uint32 conversationID, bool newTab, bool historyMove)
{
    Defer([=]
    {
        if (auto* currentViewer = dynamic_cast<ConversationViewer*>(m_currentViewer); currentViewer && !newTab)
        {
            if (currentViewer->ConversationID == conversationID)
                return;

            auto const id = currentViewer->ID;
            auto historyPrev = std::move(currentViewer->HistoryPrev);
            auto historyNext = std::move(currentViewer->HistoryNext);
            if (!historyMove)
            {
                historyPrev.emplace(currentViewer->ConversationID);
                historyNext = { };
            }
            currentViewer->~ConversationViewer();
            new(currentViewer) ConversationViewer(id, newTab, conversationID);
            currentViewer->HistoryPrev = std::move(historyPrev);
            currentViewer->HistoryNext = std::move(historyNext);
        }
        else
            m_viewers.emplace_back(new ConversationViewer(m_nextViewerID++, newTab, conversationID));
    });
}
void UI::OpenEvent(EventID eventID, bool newTab, bool historyMove)
{
    Defer([=]
    {
        if (auto* currentViewer = dynamic_cast<EventViewer*>(m_currentViewer); currentViewer && !newTab)
        {
            if (currentViewer->EventID == eventID)
                return;

            auto const id = currentViewer->ID;
            auto historyPrev = std::move(currentViewer->HistoryPrev);
            auto historyNext = std::move(currentViewer->HistoryNext);
            if (!historyMove)
            {
                historyPrev.emplace(currentViewer->EventID);
                historyNext = { };
            }
            currentViewer->~EventViewer();
            new(currentViewer) EventViewer(id, newTab, eventID);
            currentViewer->HistoryPrev = std::move(historyPrev);
            currentViewer->HistoryNext = std::move(historyNext);
        }
        else
            m_viewers.emplace_back(new EventViewer(m_nextViewerID++, newTab, eventID));
    });
}
void UI::OpenWorldMap(bool newTab)
{
    Defer([=]
    {
        m_viewers.emplace_back(new MapLayoutViewer(m_nextViewerID++, newTab));
    });
}
