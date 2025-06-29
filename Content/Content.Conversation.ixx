module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.Content.Conversation;
import GW2Viewer.Common;
import GW2Viewer.Data.Game;
import GW2Viewer.Utils.String;
import std;

export namespace Content
{

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
            if (auto [string, status] = G::Game.Text.Get(speakerNameTextID); string)
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
                if (auto string = G::Game.Text.Get(speakerNameTextID).first)
                    result = std::format("{}{}{}", result, result.empty() ? "" : ", ", Utils::Encoding::ToUTF8(*string));
        return result;
    }
    std::string StartingStateText() const
    {
        std::string result;
        for (auto const& state : States | std::views::filter([](auto const& state) { return state.TextID; }))
            if (result.empty() || state.IsStart())
                if (auto string = G::Game.Text.Get(state.TextID).first)
                    if (result = Utils::Encoding::ToUTF8(*string), state.IsStart())
                        break;

        Utils::String::ReplaceAll(result, "\r", R"(<c=#F00>\r</c>)");
        Utils::String::ReplaceAll(result, "\n", R"(<c=#F00>\n</c>)");
        return result;
    }
    std::chrono::system_clock::time_point EncounteredTime;
    uint32 Session { };
    uint32 Map { };
    ImVec4 Position { };
};
std::map<uint32, struct Conversation> conversations;
std::shared_mutex conversationsLock;

}
