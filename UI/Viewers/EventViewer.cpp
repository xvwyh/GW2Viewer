module GW2Viewer.UI.Viewers.EventViewer;
import GW2Viewer.Common.Time;
import GW2Viewer.Content;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Text.Format;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Manager;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Exception;
import GW2Viewer.Utils.Format;
import magic_enum;
#include "Macros.h"

namespace GW2Viewer::UI::Viewers
{

void EventViewer::Cache::Data::StoreHeight()
{
    if (I::GetCurrentWindow()->DC.CursorMaxPos.y - I::GetCurrentTable()->RowPosY1 <= 5.0f)
        I::Dummy({ 0, 10 });
    Height = I::GetCurrentWindow()->DC.CursorMaxPos.y - I::GetCurrentTable()->RowPosY1;
}

std::string EventViewer::Title()
{
    std::shared_lock _(Content::eventsLock);
    return std::format("<c=#4>{} </c>{}", Base::Title(), Utils::Encoding::ToUTF8(Content::events.at(EventID).Title()));
}

void EventViewer::Draw()
{
    if (scoped::Child(I::GetSharedScopeID("EventViewer"), { }, ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
    {
        DrawHistoryButtons();
        I::SameLine();
        I::Checkbox("Enemy Perspective", &Invert);
        I::SameLine();
        I::Checkbox("In Dungeon Map", &InDungeonMap);
        I::SameLine();
        I::SetNextItemWidth(-FLT_MIN);
        int progress = PreviewProgress * 100;
        I::SliderInt("##Progress", &progress, 0, 100, "Progress: %u%%");
        PreviewProgress = progress / 100.0f;
    }

    std::shared_lock _(Content::eventsLock);

    if (scoped::Child("###EventViewer-Table", { 400, 0 }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
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
    if (I::SameLine(); scoped::Child("###EventViewer-Details", { }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Borders))
    {
        if (Selected)
        {
            auto const eventID = Selected->first;
            auto const& event = Content::events.at(eventID);
            auto& cache = Cache[eventID];
            if (Selected->second)
            {
                if (!event.Objectives.empty())
                {
                    Data::Content::ContentObject dummyContent { };
                    auto const objectiveIndex = *Selected->second;
                    auto variants = event.Objectives | std::views::filter([objectiveIndex](Content::Event::Objective const& objective) { return objective.EventObjectiveIndex == objectiveIndex; });
                    auto const& objective = eventID.UID ? *std::next(variants.begin(), cache.Objectives.at(objectiveIndex).SelectedVariant) : *std::next(event.Objectives.begin(), objectiveIndex);
                    I::PushItemWidth(-FLT_MIN);
                    I::InputTextReadOnly("Map", std::format("{}", objective.Map)); Controls::ContentButton(G::Game.Content.GetByDataID(Content::MapDef, objective.Map), &objective.Map);
                    I::InputTextReadOnly("Event UID", std::format("{}", objective.EventUID));
                    I::InputTextReadOnly("Event Objective Index", std::format("{}", objective.EventObjectiveIndex));
                    if (auto const itrType = G::Config.SharedEnums.find("ObjectiveType"); itrType != G::Config.SharedEnums.end())
                    {
                        Data::Content::TypeInfo::Symbol dummySymbol { .Enum = Data::Content::TypeInfo::Enum {.Name = "ObjectiveType" } };
                        I::AlignTextToFramePadding(); I::TextUnformatted("Type:"); I::SameLine(); Data::Content::Symbols::GetByName("uint32")->Draw({ &objective.Type, dummyContent, dummySymbol });
                    }
                    else
                        I::InputTextReadOnly("Type", std::format("{}", objective.Type));
                    I::InputTextReadOnly("Flags", std::format("0x{:X}", objective.Flags));
                    I::InputTextReadOnly("TargetCount", std::format("{}", objective.TargetCount));
                    I::InputTextReadOnly("ProgressBarStyle", std::format("{}", objective.ProgressBarStyle)); Controls::ContentButton(G::Game.Content.GetByGUID(objective.ProgressBarStyle), &objective.ProgressBarStyle);

                    Data::Content::TypeInfo::Symbol dummySymbol;
                    I::AlignTextToFramePadding(); I::TextUnformatted("Text:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &objective.TextID, dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("AgentName:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &objective.AgentNameTextID, dummyContent, dummySymbol });

                    I::InputTextReadOnly("ExtraInt", std::format("{}", objective.ExtraInt));
                    I::InputTextReadOnly("ExtraInt2", std::format("{}", objective.ExtraInt2));
                    I::InputTextReadOnly("ExtraGUID", std::format("{}", objective.ExtraGUID)); Controls::ContentButton(G::Game.Content.GetByGUID(objective.ExtraGUID), &objective.ExtraGUID);
                    I::InputTextReadOnly("ExtraGUID2", std::format("{}", objective.ExtraGUID2)); Controls::ContentButton(G::Game.Content.GetByGUID(objective.ExtraGUID2), &objective.ExtraGUID2);
                    I::PopItemWidth();
                    if (!objective.ExtraBlob.empty())
                    {
                        auto _ = Utils::Exception::SEHandler::Create();
                        I::TextUnformatted("ExtraBlob:");
                        Controls::HexViewerOptions options;
                        HexViewer(objective.ExtraBlob, options);
                    }
                }
            }
            else
            {
                if (!event.States.empty())
                {
                    auto const& state = *std::next(event.States.begin(), cache.Event.SelectedVariant);
                    I::PushItemWidth(-FLT_MIN);
                    I::InputTextReadOnly("Map", std::format("{}", state.Map)); Controls::ContentButton(G::Game.Content.GetByDataID(Content::MapDef, state.Map), &state.Map);
                    I::InputTextReadOnly("Event UID", std::format("{}", state.UID));
                    I::InputTextReadOnly("Level", std::format("{}", state.Level));
                    I::InputTextReadOnly("Flags (client)", std::format("0x{:X}\n{}", std::to_underlying(state.FlagsClient), magic_enum::enum_flags_name(state.FlagsClient, '\n')), ImGuiInputTextFlags_Multiline);
                    I::InputTextReadOnly("Flags (server)", std::format("0x{:X}\n{}", std::to_underlying(state.FlagsServer), magic_enum::enum_flags_name(state.FlagsServer, '\n')), ImGuiInputTextFlags_Multiline);
                    I::InputTextReadOnly("AudioEffect", std::format("{}", state.AudioEffect)); Controls::ContentButton(G::Game.Content.GetByGUID(state.AudioEffect), &state.AudioEffect);
                    I::InputTextReadOnly("A", std::format("{}", state.A));

                    Data::Content::ContentObject dummyContent { };
                    Data::Content::TypeInfo::Symbol dummySymbol;
                    I::AlignTextToFramePadding(); I::TextUnformatted("Title:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.TitleTextID, dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str1%:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.TitleParameterTextID[0], dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str2%:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.TitleParameterTextID[1], dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str3%:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.TitleParameterTextID[2], dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str4%:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.TitleParameterTextID[3], dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str5%:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.TitleParameterTextID[4], dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("%str6%:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.TitleParameterTextID[5], dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("Description:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.DescriptionTextID, dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("MetaText:"); I::SameLine(); Data::Content::Symbols::GetByName("StringID")->Draw({ &state.MetaTextTextID, dummyContent, dummySymbol });
                    I::AlignTextToFramePadding(); I::TextUnformatted("Icon:"); I::SameLine(); Data::Content::Symbols::GetByName("FileID")->Draw({ &state.FileIconID, dummyContent, dummySymbol });
                    I::PopItemWidth();
                    Controls::Texture(state.FileIconID);
                }
            }
        }
    }
}

void EventViewer::DrawEvent(Content::EventID eventID)
{
    using namespace Data::Text;

    auto& cache = Cache[eventID];
    auto const& event = Content::events.at(eventID);
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
                Controls::Texture(state.FileIconID ? state.FileIconID : 102388, { .Size = { 32, 32 }, .ReserveSpace = true });

        if (scoped::Table("Title", 3, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Hideable, { -FLT_MIN, 0 }))
        if (scoped::Font(G::UI.Fonts.GameHeading, 18.0f))
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
                I::TextWrapped("<b>%s</b>", Utils::Encoding::ToUTF8(title).c_str());
            }

            I::TableNextColumn();

            I::TableNextColumn();
            I::Text("<b>%s</b>", Utils::Encoding::ToUTF8(FormatString(301769, TEXTPARAM_NUM1, state.Level)).c_str());
        }

        if (state.MetaTextTextID)
            if (scoped::Font(G::UI.Fonts.GameText, 14.725f))
                I::TextWrapped("%s", Utils::Encoding::ToUTF8(FormatString(state.MetaTextTextID)).c_str());

        cache.Event.StoreHeight();
    }

    if (eventID.UID)
    {
        uint32 const objectiveIndexCount = event.Objectives.empty() ? 0 : std::ranges::max(event.Objectives, { }, &Content::Event::Objective::EventObjectiveIndex).EventObjectiveIndex + 1;
        cache.Objectives.resize(std::max<size_t>(cache.Objectives.size(), objectiveIndexCount));
        for (uint32 objectiveIndex = 0; objectiveIndex < objectiveIndexCount; ++objectiveIndex)
        {
            auto variants = event.Objectives | std::views::filter([objectiveIndex](Content::Event::Objective const& objective) { return objective.EventObjectiveIndex == objectiveIndex; });
            uint32 variantCount = std::ranges::count(event.Objectives, objectiveIndex, &Content::Event::Objective::EventObjectiveIndex);
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

void EventViewer::DrawObjective(Content::Event::Objective const& objective, Cache::Data& cache, uint32 index, uint32* variant, uint32 const* variantCount)
{
    using namespace Data::Text;

    struct Params
    {
        Content::Event::Objective const& Objective;
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
                    I::TextWrapped("%s", Utils::Encoding::ToUTF8(FormatString(49787,
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
                        Controls::Texture(icon, { .Size = { iconSize, iconSize }, .ReserveSpace = true });
                    //I::NewLine();
                    I::SetCursorScreenPos(cursor);
                }
                else
                {
                    Controls::Texture(icon, { .Size = { iconSize, iconSize }, .ReserveSpace = true });
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
                    time = std::vformat(params.TargetCount >= 60 * 60 ? L"{:%H:%M:%S}" : params.TargetCount >= 60 ? L"{:%M:%S}" : L"{:%S}", std::make_wformat_args(Time::Secs(count)));
                    time = FormatString(47773,
                        TEXTPARAM_STR1_LITERAL, defend ? L"#C5331B" : L"#BFD47A",
                        TEXTPARAM_STR2_LITERAL, std::wstring_view { time.begin() + time.starts_with(L'0'), time.end() });
                    if (*params.WarningTime && count <= *params.WarningTime)
                    {
                        float value;
                        if (params.TextDefault == 192867)
                            value = std::max(0.0f, 1.0f - std::abs(1000 - Time::ToTimestampMs(Time::Now()) % 2000) / 1000.0f);
                        else
                            value = std::max(0.0f, 3.0f * (1000 - Time::ToTimestampMs(Time::Now()) % 1000) / 1000.0f - 2.0f);
                        color = I::ColorLerp(0xFFFFFFFF, 0xFF0000FF, value);
                    }
                }
                if (scoped::WithColorVar(ImGuiCol_Text, color))
                    I::TextWrapped("%s", Utils::Encoding::ToUTF8(FormatString(params.Text ? params.Text : inverted ? params.TextDefaultInverted : params.TextDefault,
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
                .AgentNameLiteral = FormatString(49808, TEXTPARAM_STR1_LITERAL, G::Game.Content.GetByGUID(params.Objective.ExtraGUID)->GetDisplayName()),
                .TextDefault = params.Objective.Flags & 0x2 ? 777812u : 47250,
                .TargetCount = params.Objective.TargetCount,
                .Icon = 1,
                .IconFileID = G::Game.Content.GetByGUID(params.Objective.ExtraGUID)->GetIcon(),
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
            auto const leaderboardObjectiveDef = G::Game.Content.GetByGUID(params.Objective.ExtraGUID);
            auto const leaderboardDef = G::Game.Content.GetByGUID(params.Objective.ExtraGUID2);

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
                    .ProgressBarStyleDef = G::Game.Content.GetByGUID(params.Objective.ProgressBarStyle),
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
                .DisplayedAsProgressBar = params.Objective.Flags & 0x2,
                .Invert = params.Objective.Flags & 0x1,
                .InvertProgress = !(params.Objective.Flags & 0x4),
                .AgentName = params.Objective.AgentNameTextID,
                .TextDefault = 47096,
                .TargetCount = params.Objective.TargetCount,
                .Icon = 7,
                .IconInverted = 5,
            });
        } },
        { { "DefendPlacedGadget", "DefendSpawnedGadget", "DefendSpawnedGadgets" }, [](Params const& params)
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
                .ProgressBarStyleDef = G::Game.Content.GetByGUID(params.Objective.ProgressBarStyle),
            });
        } },
        { { "DestroyPlacedGadget", "DestroySpawnedGadget", "DestroySpawnedGadgets" }, [](Params const& params)
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
                .ProgressBarStyleDef = G::Game.Content.GetByGUID(params.Objective.ProgressBarStyle),
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
            I::TextWrapped("%s", Utils::Encoding::ToUTF8(FormatString(params.Objective.TextID, TEXTPARAM_NUM1, 1)).c_str());
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
                if (Content::EventID const eventID { params.Objective.Map, entry.EventUID }; Content::events.contains(eventID))
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
        { { "LevelUp" }, [](Params const& params)
        {

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
                        I::TextWrapped("%s", Utils::Encoding::ToUTF8(FormatString(302045)).c_str());
                        break;
                    case 3:
                        I::TextWrapped("%s", Utils::Encoding::ToUTF8(FormatString(302020)).c_str());
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
                I::TextWrapped("%s", Utils::Encoding::ToUTF8(FormatString(302270)).c_str());
                if (I::Button(Utils::Encoding::ToUTF8(FormatString(48858)).c_str()) && !url.empty())
                    I::OpenURL(url.c_str());
                I::SameLine();
                if (I::Button(Utils::Encoding::ToUTF8(FormatString(49022)).c_str()))
                    I::CloseCurrentPopup();
            }
        } },
        { { "Push" }, [](Params const& params)
        {
            I::Text("%f", ((float*)params.Objective.ExtraBlob.data())[0]);
            I::Text("%f", ((float*)params.Objective.ExtraBlob.data())[1]);
            I::Text("%f", ((float*)params.Objective.ExtraBlob.data())[2]);
        } },
        { { "Manual", "QuestManual" }, [](Params const& params)
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
                .ProgressBarStyleDef = G::Game.Content.GetByGUID(params.Objective.ProgressBarStyle),
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
            auto const wvwObjectiveDef = G::Game.Content.GetByGUID(params.Objective.ExtraGUID);
            params.DrawProgress({
                .DisplayedAsProgressBar = false,
                .AgentName = wvwObjectiveDef ? (uint32)(*wvwObjectiveDef)["TextName"] : 0,
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
            auto const upgradeLineDef = G::Game.Content.GetByGUID(params.Objective.ExtraGUID);
            auto const wvwObjectiveDef = G::Game.Content.GetByGUID(params.Objective.ExtraGUID2);
            if (upgradeLineDef && params.Objective.ExtraInt)
            {
                uint32 const tier = params.Objective.ExtraInt - 1;
                uint32 const textName = *std::next((*upgradeLineDef)["WvwObjectiveUpgradeInfo->TextName"].begin(), tier);
                uint32 const buildTime = *std::next((*upgradeLineDef)["WvwObjectiveUpgradeInfo->BuildTime"].begin(), tier);
                uint32 const supplyCost = *std::next((*upgradeLineDef)["WvwObjectiveUpgradeInfo->SupplyCost"].begin(), tier);
                if (PreviewProgress >= 0.5f && buildTime)
                {
                    uint32 remainingTime = buildTime * (1.0f - (PreviewProgress * 2 - 1.0f));
                    float remainingFraction = (float)remainingTime / (float)buildTime;
                    params.DrawProgress({
                        .DisplayedAsProgressBar = params.Objective.Flags & 0x1,
                        .AgentName = textName,
                        .TextDefault = 174716,
                        .TargetCount = 100,
                        .Count = (uint32)((1.0f - remainingFraction) * 100.0f),
                    });
                }
                else
                {
                    uint32 remainingSupply = supplyCost * (1.0f - PreviewProgress * 2);
                    params.DrawProgress({
                        .DisplayedAsProgressBar = params.Objective.Flags & 0x1,
                        .AgentName = textName,
                        .TextDefault = 49668,
                        .TargetCount = supplyCost,
                        .Count = supplyCost - remainingSupply,
                    });
                }
            }
        } },
        { { "GuildUpgrade" }, [](Params const& params)
        {
            auto const guildClaimableDef = G::Game.Content.GetByGUID(params.Objective.ExtraGUID);
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
    static Content::Event::Objective objective;
    if (static uint32 persist = 0; persist != objectivex.EventUID)
    {
        persist = objectivex.EventUID;
        objective =
        {
            .Type = 24,
            .Flags = (uint32)(rand() % 2),
            .ExtraInt = (uint32)(1 + rand() % 2),
            .ExtraGUID = *(*std::next(G::Game.Content.GetType(std::ranges::find_if(G::Config.TypeInfo, [](auto const& pair) { return pair.second.Name == "WvwObjectiveUpgradeLineDef"; })->first)->Objects.begin(), rand() % 10))->GetGUID(),
            .ExtraGUID2 = *(*std::next(G::Game.Content.GetType(std::ranges::find_if(G::Config.TypeInfo, [](auto const& pair) { return pair.second.Name == "WvwObjectiveDef"; })->first)->Objects.begin(), rand() % 10))->GetGUID(),
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

    Content::EventID const eventID { objective.Map, objective.EventUID };

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

    if (auto const itrType = G::Config.SharedEnums.find("ObjectiveType"); itrType != G::Config.SharedEnums.end())
        if (auto const itrValue = itrType->second.Values.find(objective.Type); itrValue != itrType->second.Values.end())
            if (auto const itrDraw = std::ranges::find_if(drawObjectiveType, [name = itrValue->second](auto const& pair) { return std::ranges::contains(pair.first, name); }); itrDraw != drawObjectiveType.end())
                if (scoped::Font(G::UI.Fonts.GameText, 14.725f))
                    itrDraw->second({ .Objective = objective, .Viewer = *this, .Cache = cache });

    if (!cache.Height)
        cache.StoreHeight();
}

}
