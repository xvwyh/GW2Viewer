module;
#include "UI/ImGui/ImGui.h"
#include "Utils/Async.h"

export module GW2Viewer.UI.Viewers.ConversationListViewer;
import GW2Viewer.Content.Conversation;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.ConversationViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.Scan;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct ConversationListViewer : ListViewer<ConversationListViewer, { ICON_FA_COMMENT_CHECK " Conversations", "Conversations", Category::ListViewer }>
{
    ConversationListViewer(uint32 id, bool newTab) : Base(id, newTab) { }

    std::shared_mutex Lock;
    std::vector<uint32> FilteredList;
    Utils::Async::Scheduler AsyncFilter { true };

    std::string FilterString;
    std::optional<std::pair<int32, int32>> FilterID;
    uint32 FilterRange { };
    enum class ConversationSort { GenID, UID, StartingSpeakerName, StartingStateText, EncounteredTime } Sort { ConversationSort::UID };
    bool SortInvert { };

    static auto SortList(Utils::Async::Context context, std::vector<uint32>& data, ConversationSort sort, bool invert)
    {
        std::scoped_lock _(Content::conversationsLock);
        switch (sort)
        {
            using enum ConversationSort;
            case GenID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a < b ^ invert; });
                break;
            case UID:
                ComplexSort(data, invert, [](uint32 id) { return Content::conversations.at(id).UID; });
                break;
            case StartingSpeakerName:
                ComplexSort(data, invert, [](uint32 id) { return Content::conversations.at(id).StartingSpeakerName(); });
                break;
            case StartingStateText:
                ComplexSort(data, invert, [](uint32 id) { return Content::conversations.at(id).StartingStateText(); });
                break;
            case EncounteredTime:
                ComplexSort(data, invert, [](uint32 id) { return Content::conversations.at(id).EncounteredTime; });
                break;
            default: std::terminate();
        }
    }
    void SetResult(Utils::Async::Context context, std::vector<uint32>&& data)
    {
        if (context->Cancelled) return;
        std::unique_lock _(Lock);
        FilteredList = std::move(data);
        context->Finish();
    }
    void UpdateSort()
    {
        AsyncFilter.Run([this, sort = Sort, invert = SortInvert](Utils::Async::Context context)
        {
            context->SetIndeterminate();
            std::vector<uint32> data;
            {
                std::shared_lock _(Lock);
                data = FilteredList;
            }
            CHECK_ASYNC;
            SortList(context, data, sort, invert);
            SetResult(context, std::move(data));
        });
    }
    void UpdateSearch()
    {
        bool textSearch = false;
        FilterID.reset();
        if (FilterString.empty())
            ;
        else if (uint32 id, range; Utils::Scan::Into(FilterString, "{}+{}", id, range))
            FilterID.emplace(id - range, id + range);
        else if (Utils::Scan::Into(FilterString, "{}-{}", id, range))
            FilterID.emplace(id, range);
        else if (Utils::Scan::NumberLiteral(FilterString, id))
            FilterID.emplace(id - FilterRange, id + FilterRange);
        else if (Utils::Scan::NumberLiteral(FilterString, "0x{:x}", id))
            FilterID.emplace(id - FilterRange, id + FilterRange);
        else
            textSearch = true;

        AsyncFilter.Run([this, textSearch, filter = FilterID, string = FilterString, sort = Sort, invert = SortInvert](Utils::Async::Context context) mutable
        {
            context->SetIndeterminate();
            std::vector<uint32> data;
            CHECK_ASYNC;
            if (textSearch)
            {
                std::scoped_lock _(Content::conversationsLock);
                std::wstring const query(std::from_range, Utils::Encoding::FromUTF8(string) | std::views::transform(toupper));
                data.assign_range(Content::conversations | std::views::keys | std::views::filter([&query](uint32 id)
                {
                    auto const& conversation = Content::conversations.at(id);
                    for (auto const& state : conversation.States)
                    {
                        if (auto const string = G::Game.Text.GetNormalized(state.TextID).first; string && !string->empty() && string->contains(query))
                            return true;
                        if (auto const string = G::Game.Text.GetNormalized(state.SpeakerNameTextID).first; string && !string->empty() && string->contains(query))
                            return true;
                        for (auto const& transition : state.Transitions)
                            if (auto const string = G::Game.Text.GetNormalized(transition.TextID).first; string && !string->empty() && string->contains(query))
                                return true;
                    }
                    return false;
                }));
            }
            else
            {
                auto limits = filter.value_or(std::pair { std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() });
                std::scoped_lock _(Content::conversationsLock);
                data.assign_range(Content::conversations | std::views::filter([limits](auto const& pair) { return (int32)pair.second.UID >= limits.first && (int32)pair.second.UID <= limits.second; }) | std::views::keys);
            }
            CHECK_ASYNC;
            SortList(context, data, sort, invert);
            SetResult(context, std::move(data));
        });
    }

    void Draw() override
    {
        I::SetNextItemWidth(-(I::GetStyle().ItemSpacing.x + 60));
        if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &FilterString))
            UpdateSearch();
        Controls::AsyncProgressBar(AsyncFilter);
        I::SameLine();
        if (scoped::Disabled(!FilterID))
        {
            I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
            if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&FilterRange, 0.1f, 0, 10000))
                UpdateSearch();
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
                Sort = (ConversationSort)specs->Specs[0].ColumnUserID;
                SortInvert = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                specs->SpecsDirty = false;
                UpdateSort();
            }

            [&](auto _) -> void
            {
                std::scoped_lock __(Lock);
                ImGuiListClipper clipper;
                clipper.Begin(FilteredList.size());
                while (clipper.Step())
                {
                    for (auto conversationID : std::span(FilteredList.begin() + clipper.DisplayStart, FilteredList.begin() + clipper.DisplayEnd))
                    {
                        scoped::WithID(conversationID);

                        std::scoped_lock ___(Content::conversationsLock);
                        auto& conversation = Content::conversations.at(conversationID);
                        auto const* currentViewer = G::UI.GetCurrentViewer<ConversationViewer>();
                        I::TableNextRow();

                        I::TableNextColumn();
                        I::Selectable(std::format("  <c=#4>{}</c>", conversationID).c_str(), currentViewer && currentViewer->ConversationID == conversationID ? ImGuiTreeNodeFlags_Selected : 0, ImGuiSelectableFlags_SpanAllColumns);

                        I::GetWindowDrawList()->AddRectFilled(I::GetCurrentContext()->LastItemData.Rect.Min, { I::GetCurrentContext()->LastItemData.Rect.Min.x + 4, I::GetCurrentContext()->LastItemData.Rect.Max.y }, IM_COL32(0xFF, 0x00, 0x00, (byte)std::lerp(0xFF, 0x00, conversation.GetCompleteness() / (float)Content::Conversation::COMPLETENESS_COMPLETE)));

                        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                            ConversationViewer::Open(conversationID, { .MouseButton = button });

                        I::TableNextColumn();
                        I::Text("<c=#8>%u</c>", conversation.UID);

                        I::TableNextColumn();
                        I::TextUnformatted(conversation.StartingSpeakerName().c_str());

                        I::TableNextColumn();
                        I::TextUnformatted(conversation.StartingStateText().c_str());

                        I::TableNextColumn();
                        if (conversation.EncounteredTime.time_since_epoch().count())
                        {
                            if (I::Selectable(std::format("<c=#{}>{}</c> {}###EncounteredTime", conversation.Map ? "F" : "2", ICON_FA_GLOBE, Utils::Format::DurationShortColored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - conversation.EncounteredTime))).c_str()))
                            {
                                // TODO: Open map to { conversation.Map, conversation.Position }
                            }
                            if (scoped::ItemTooltip())
                                I::TextUnformatted(std::format("Encountered on: {}", Utils::Format::DateTimeFullLocal(conversation.EncounteredTime)).c_str());
                        }
                    }
                }
            }(0);
        }
    }
};

}
