module;
#include "UI/ImGui/ImGui.h"
#include "Utils/Async.h"

export module GW2Viewer.UI.Viewers.EventListViewer;
import GW2Viewer.Content.Event;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.EventViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.Scan;

export namespace GW2Viewer::UI::Viewers
{

struct EventListViewer : ListViewer<EventListViewer>
{
    EventListViewer(uint32 id, bool newTab) : ListViewer(id, newTab) { }

    std::shared_mutex Lock;
    std::vector<Content::EventID> FilteredList;
    Utils::Async::Scheduler AsyncFilter { true };

    std::string FilterString;
    std::optional<std::pair<int32, int32>> FilterID;
    uint32 FilterRange { };
    struct { bool Normal = true, Group = true, Meta = true, Dungeon = true, NonEvent = true; } Filters;
    enum class EventSort { ID, Map, Type, Title, EncounteredTime } Sort { EventSort::ID };
    bool SortInvert { };

    static void SortList(Utils::Async::Context context, std::vector<Content::EventID>& data, EventSort sort, bool invert)
    {
        std::scoped_lock _(Content::eventsLock);
        switch (sort)
        {
            using enum EventSort;
            case ID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a.UID < b.UID ^ invert; });
                break;
            case Map:
                ComplexSort(data, invert, [](Content::EventID id) { return Content::events.at(id).Map(); });
                break;
            case Type:
                ComplexSort(data, invert, [](Content::EventID id) { return Content::events.at(id).Type(); });
                break;
            case Title:
                ComplexSort(data, invert, [](Content::EventID id) { return Content::events.at(id).Title(); });
                break;
            case EncounteredTime:
                ComplexSort(data, invert, [](Content::EventID id) { return Content::events.at(id).EncounteredTime(); });
                break;
            default: std::terminate();
        }
    }
    void SetResult(Utils::Async::Context context, std::vector<Content::EventID>&& data)
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
            std::vector<Content::EventID> data;
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

        AsyncFilter.Run([this, textSearch, filter = FilterID, string = FilterString, filters = Filters, sort = Sort, invert = SortInvert](Utils::Async::Context context) mutable
        {
            context->SetIndeterminate();
            std::vector<Content::EventID> data;
            if (!textSearch)
            {
                auto limits = filter.value_or(std::pair { std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() });
                std::scoped_lock _(Content::eventsLock);
                data.assign_range(Content::events | std::views::keys | std::views::filter([limits](Content::EventID id) { return (int32)id.UID >= limits.first && (int32)id.UID <= limits.second; }));
            }
            else
            {
                std::scoped_lock _(Content::eventsLock);
                data.assign_range(Content::events | std::views::keys);
            }
            CHECK_ASYNC;
            if (!(filters.Normal && filters.Group && filters.Meta && filters.Dungeon && filters.NonEvent))
            {
                std::scoped_lock _(Content::eventsLock);
                std::erase_if(data, [&filters](Content::EventID id)
                {
                    auto const& event = Content::events.at(id);
                    return !(
                        !id.UID && filters.NonEvent ||
                        id.UID && std::ranges::any_of(event.States, &Content::Event::State::IsDungeonEvent) && filters.Dungeon ||
                        id.UID && std::ranges::any_of(event.States, &Content::Event::State::IsMetaEvent) && filters.Meta ||
                        id.UID && std::ranges::any_of(event.States, &Content::Event::State::IsGroupEvent) && filters.Group ||
                        id.UID && std::ranges::any_of(event.States, &Content::Event::State::IsNormalEvent) && filters.Normal
                    );
                });
            }
            CHECK_ASYNC;
            if (textSearch)
            {
                std::scoped_lock _(Content::eventsLock);
                std::wstring const query(std::from_range, Utils::Encoding::FromUTF8(string) | std::views::transform(toupper));
                std::erase_if(data, [&query](Content::EventID id)
                {
                    auto const& event = Content::events.at(id);
                    for (auto const& state : event.States)
                    {
                        if (auto const string = G::Game.Text.GetNormalized(state.TitleTextID).first; string && !string->empty() && string->contains(query))
                            return false;
                        for (auto const& param : state.TitleParameterTextID)
                            if (auto const string = G::Game.Text.GetNormalized(param).first; string && !string->empty() && string->contains(query))
                                return false;
                        if (auto const string = G::Game.Text.GetNormalized(state.DescriptionTextID).first; string && !string->empty() && string->contains(query))
                            return false;
                        if (auto const string = G::Game.Text.GetNormalized(state.MetaTextTextID).first; string && !string->empty() && string->contains(query))
                            return false;
                    }
                    for (auto const& objective : event.Objectives)
                    {
                        if (auto const string = G::Game.Text.GetNormalized(objective.TextID).first; string && !string->empty() && string->contains(query))
                            return false;
                        if (auto const string = G::Game.Text.GetNormalized(objective.AgentNameTextID).first; string && !string->empty() && string->contains(query))
                            return false;
                    }
                    if (std::wstring const map { std::from_range, event.Map() | std::views::transform(toupper) }; map.contains(query))
                        return false;
                    return true;
                });
            }
            CHECK_ASYNC;
            SortList(context, data, sort, invert);
            SetResult(context, std::move(data));
        });
    }
    void UpdateFilter() { UpdateSearch(); }

    std::string Title() override { return ICON_FA_SEAL " Events"; }
    void Draw() override
    {
        I::SetNextItemWidth(-(I::GetStyle().ItemSpacing.x + 60));
        if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &FilterString))
            UpdateSearch();
        if (auto context = AsyncFilter.Current())
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
        if (scoped::Disabled(!FilterID))
        {
            I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
            if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&FilterRange, 0.1f, 0, 10000))
                UpdateSearch();
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
                UpdateFilter();
            }
        };
        filter("Normal", Filters.Normal);
        filter("Group", Filters.Group);
        filter("Meta", Filters.Meta);
        filter("Dungeon", Filters.Dungeon);
        filter("Non-Event", Filters.NonEvent);

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
                Sort = (EventSort)specs->Specs[0].ColumnUserID;
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
                    for (auto eventID : std::span(FilteredList.begin() + clipper.DisplayStart, FilteredList.begin() + clipper.DisplayEnd))
                    {
                        scoped::WithID(eventID.Map << 17 | eventID.UID);

                        std::scoped_lock ___(Content::eventsLock);
                        auto& event = Content::events.at(eventID);
                        auto const* currentViewer = G::UI.GetCurrentViewer<EventViewer>();
                        I::TableNextRow();

                        I::TableNextColumn();
                        I::Selectable(std::format("{}", eventID.UID).c_str(), currentViewer && currentViewer->EventID == eventID ? ImGuiTreeNodeFlags_Selected : 0, ImGuiSelectableFlags_SpanAllColumns);
                        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                            EventViewer::Open(eventID, { .MouseButton = button });

                        I::TableNextColumn();
                        I::TextUnformatted(Utils::Encoding::ToUTF8(event.Map()).c_str());

                        I::TableNextColumn();
                        I::TextUnformatted(event.Type().c_str());

                        I::TableNextColumn();
                        I::TextUnformatted(Utils::Encoding::ToUTF8(event.Title()).c_str());

                        I::TableNextColumn();
                        if (auto time = event.EncounteredTime(); time.time_since_epoch().count())
                        {
                            if (I::Selectable(std::format("<c=#{}>{}</c> {}###EncounteredTime", eventID.Map ? "F" : "2", ICON_FA_GLOBE, Utils::Format::DurationShortColored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - time))).c_str()))
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
};

}
