module;
#include "UI/ImGui/ImGui.h"

module GW2Viewer.UI.Viewers.ConversationViewer;
import GW2Viewer.Content.Conversation;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.String;
import std;
import <gsl/gsl>;

namespace GW2Viewer::UI::Viewers
{

void ConversationViewer::Draw()
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

    std::shared_lock _(Content::conversationsLock);
    auto& conversation = Content::conversations.at(ConversationID);
    bool const conversationHasMultipleSpeakers = conversation.HasMultipleSpeakers();
    bool const conversationHasScriptedStart = conversation.HasScriptedStart();

    bool wikiWrite = false;
    uint32 wikiDepth = 0;
    std::string wikiBuffer;
    auto wiki = std::back_inserter(wikiBuffer);
    static bool drawStateTypeIcons = true, drawStateTypeText = false, drawSpeakerName = false, drawEncryptionStatus = false, drawTextID = false, drawEncounterInfo = false;
    if (scoped::Child(I::GetSharedScopeID("ConversationViewer"), { }, ImGuiChildFlags_Border | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
    {
        DrawHistoryButtons();
        I::SameLine();
        if (I::Button(ICON_FA_COPY " Wiki Markup"))
        {
            wikiWrite = true;
            wikiBuffer.reserve(64 * 1024);
        }
        I::SameLine(); I::Checkbox("State Type Icons", &drawStateTypeIcons);
        I::SameLine(); I::Checkbox("State Type Text", &drawStateTypeText);
        I::SameLine(); I::Checkbox("Speaker Name", &drawSpeakerName);
        I::SameLine(); I::Checkbox("Text ID", &drawTextID);
        I::SameLine(); I::Checkbox("Encryption Status", &drawEncryptionStatus);
        I::SameLine(); I::Checkbox("Encounter Info", &drawEncounterInfo);
    }

    auto drawState = [&](bool parentOpen, Content::Conversation::State const& state, std::set<uint32>& visitedStates, uint32& startingSpeakerNameTextID, auto& drawState) -> void
    {
        if (!startingSpeakerNameTextID)
            startingSpeakerNameTextID = state.SpeakerNameTextID;

        bool const isScriptedStartState = conversationHasScriptedStart && state.IsScriptedStateState();
        if (isScriptedStartState)
        {
            std::set targets { std::from_range, conversation.States | std::views::transform(&Content::Conversation::State::StateID) };
            targets.erase(state.StateID);
            for (auto const& state : conversation.States)
                for (auto const& transition : state.Transitions)
                    for (auto const& target : transition.Targets)
                        targets.erase(target.TargetStateID);

            uint32 i = 0;
            std::erase_if(state.ScriptedTransitions, [&targets](Content::Conversation::State::Transition const& transition) { return !targets.contains(transition.Targets.begin()->TargetStateID); });
            for (auto const& target : targets)
            {
                state.ScriptedTransitions.emplace(Content::Conversation::State::Transition
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

        auto [speaker, speakerStatus] = G::Game.Text.Get(state.SpeakerNameTextID);
        auto [string, status] = G::Game.Text.Get(state.TextID);
        std::string text = string ? Utils::Encoding::ToUTF8(*string).c_str() : "";
        Utils::String::ReplaceAll(text, "\r", R"(<c=#F00>\r</c>)");
        Utils::String::ReplaceAll(text, "\n", R"(<c=#F00>\n</c>)");

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
                draw(StateID, { });
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
                    if (auto const itr = G::Config.ConversationScriptedStartSituations.find(ConversationID); itr != G::Config.ConversationScriptedStartSituations.end())
                        if (auto const itr2 = itr->second.find(state.StateID); itr2 != itr->second.end())
                            situation = itr2->second.c_str();
                    std::format_to(wiki, "\r\n;{}:\r\n", situation);
                }

                ++wikiDepth;
                wikiBuffer.append(wikiDepth, ':');

                if (conversationHasMultipleSpeakers && state.SpeakerNameTextID && speaker)
                    std::format_to(wiki, "'''{}:''' ", Utils::Encoding::ToUTF8(*speaker));

                std::string text = string ? Utils::Encoding::ToUTF8(*string).c_str() : "";
                Utils::String::ReplaceAll(text, "\r\n", "<br>");
                Utils::String::ReplaceAll(text, "\r", "<br>");
                Utils::String::ReplaceAll(text, "\n", "<br>");
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
            I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, state.GetCompleteness() / (float)Content::Conversation::COMPLETENESS_COMPLETE)));

            if (drawEncounterInfo)
            {
                I::SameLine();
                if (state.EncounteredTime.time_since_epoch().count())
                {
                    if (I::Button(std::format("<c=#{}>{}</c> {}###EncounteredTime", state.Map ? "F" : "2", ICON_FA_GLOBE, Utils::Format::DurationShortColored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - state.EncounteredTime))).c_str()))
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
                    drawEncryptionStatus ? GetStatusText(speakerStatus) : "",
                    speaker ? Utils::Encoding::ToUTF8(*speaker).c_str() : "");
            }

            I::SameLine();
            I::Text("%s%s%s",
                drawTextID ? std::format("<c=#CCF>({})</c>", state.TextID).c_str() : "",
                drawEncryptionStatus ? GetStatusText(status) : "", 
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
                    I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, Content::Conversation::COMPLETENESS_PRESUMABLY_MISSING / (float)Content::Conversation::COMPLETENESS_COMPLETE)));
                }
                nextExpectedTransitionID = transition.TransitionID + 1;
            }

            auto [string, status] = G::Game.Text.Get(transition.TextID);
            std::string text = string ? Utils::Encoding::ToUTF8(*string).c_str() : "";
            Utils::String::ReplaceAll(text, "\r", R"(<c=#F00>\r</c>)");
            Utils::String::ReplaceAll(text, "\n", R"(<c=#F00>\n</c>)");

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
                    draw(TransitionID, { });
                    draw(TextID, 0, true);
                    draw(CostAmount, 0);
                    draw(CostType, 2);
                    draw(CostKarma, 0);
                    draw(Diplomacy, 9);
                    draw(Unk, 3);
                    draw(Personality, 10);
                    draw(Icon, { });
                    draw(SkillDefDataID, 0);
                    #undef draw
                }

                if (wikiWrite && !isScriptedStartState)
                {
                    wikiBuffer.append(wikiDepth, ':');

                    std::string text = string ? Utils::Encoding::ToUTF8(*string).c_str() : "";
                    Utils::String::ReplaceAll(text, "\r\n", "<br>");
                    Utils::String::ReplaceAll(text, "\r", "<br>");
                    Utils::String::ReplaceAll(text, "\n", "<br>");

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
                I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, transition.GetCompleteness() / (float)Content::Conversation::COMPLETENESS_COMPLETE)));

                if (drawEncounterInfo)
                {
                    I::SameLine();
                    if (transition.EncounteredTime.time_since_epoch().count())
                    {
                        if (I::Button(std::format("<c=#{}>{}</c> {}###EncounteredTime", transition.Map ? "F" : "2", ICON_FA_GLOBE, Utils::Format::DurationShortColored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - transition.EncounteredTime))).c_str()))
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
                        Controls::Texture(iconFileID, { .Size = I::GetFrameSquare() * 1.5f, .FullPreviewOnHover = false });
                    I::Dummy(I::GetFrameSquare());
                }

                I::SameLine();
                I::AlignTextToFramePadding();
                I::Text("%s%s%s",
                    drawTextID ? std::format("<c=#CCF>({})</c> ", transition.TextID).c_str() : "",
                    drawEncryptionStatus ? GetStatusText(status) : "",
                    text.c_str());

                if (isScriptedStartState)
                {
                    uint32 const target = transition.Targets.begin()->TargetStateID;

                    I::SameLine(0, 0);
                    if (EditingScriptedStartTransitionStateID.value_or(-1) == target)
                    {
                        auto& situation = G::Config.ConversationScriptedStartSituations[ConversationID][target];
                        I::SetNextItemWidth(-FLT_MIN);
                        I::SetCursorPosX(I::GetCursorPosX() - I::GetStyle().FramePadding.x);
                        if (std::exchange(EditingScriptedStartTransitionFocus, false))
                            I::SetKeyboardFocusHere();
                        if (I::InputText("##InputSituation", &situation, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            if (situation.empty())
                                G::Config.ConversationScriptedStartSituations[ConversationID].erase(target);
                            if (G::Config.ConversationScriptedStartSituations[ConversationID].empty())
                                G::Config.ConversationScriptedStartSituations.erase(ConversationID);
                            EditingScriptedStartTransitionStateID.reset();
                        }
                    }
                    else
                    {
                        char const* situation = "Always";
                        if (auto const itr = G::Config.ConversationScriptedStartSituations.find(ConversationID); itr != G::Config.ConversationScriptedStartSituations.end())
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

                for (auto const& state : conversation.States | std::views::filter([&target](Content::Conversation::State const& state) { return state.StateID == target.TargetStateID; }))
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
            I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, Content::Conversation::COMPLETENESS_PRESUMABLY_MISSING / (float)Content::Conversation::COMPLETENESS_COMPLETE)));
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

}
