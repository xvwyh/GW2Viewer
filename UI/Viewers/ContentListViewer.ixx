export module GW2Viewer.UI.Viewers.ContentListViewer;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.ContentViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.UI.Windows.ContentSearch;
import GW2Viewer.UI.Windows.Demangle;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Scan;
import GW2Viewer.Utils.Sort;
import GW2Viewer.Utils.Visitor;
import std;
import <gsl/gsl>;
#include "Macros.h"

export namespace GW2Viewer::UI::Viewers
{

struct ContentListViewer : ListViewer<ContentListViewer, { ICON_FA_FOLDER_TREE " Content", "Content", Category::ListViewer }, G::Config.UI.Viewers.ContentListViewer>
{
    ContentListViewer(uint32 id, bool newTab) : Base(id, newTab) { }

    std::shared_mutex Lock;
    Data::Content::ContentFilter ContentFilter;
    Utils::Async::Scheduler AsyncFilter { true };
    std::optional<bool> SearchResultsReady;
    std::optional<std::vector<Data::Content::ContentObject const*>> Flatten;

    Data::Content::ContentTypeInfo const* FilterType { };
    std::string FilterString;
    std::string FilterName;
    std::optional<GUID> FilterGUID;
    std::pair<uint32, uint32> FilterUID { -1, -1 };
    std::pair<uint32, uint32> FilterDataID { -1, -1 };
    enum class ContentSort { GUID, UID, DataID, Type, Name } Sort { ContentSort::GUID };
    bool SortInvert { };
    Utils::Async::Scheduler AsyncSort;

    float ScrollX = 0;
    float ScrollExpectedX = 0;
    float ScrollTargetX = 0;
    float PrevMinDrawnIndent = 0;

    struct SortedContentObjects
    {
        std::vector<Data::Content::ContentObject const*> const& Objects;
        std::mutex& Lock;
        bool SortPending;
    };
    SortedContentObjects GetSortedContentObjects(bool isNamespace, uint32 index, std::vector<Data::Content::ContentObject const*> const& entries)
    {
        struct Cache
        {
            ContentSort Sort;
            bool Invert;
            bool Flatten;
            std::vector<Data::Content::ContentObject const*> Objects;
            std::mutex Lock;
            bool Reset = true;
            bool Ready = false;
        };
        static std::vector<Cache*> queue;
        static std::mutex queueLock;
        static std::unordered_map<uint32, Cache> namespaces, rootObjects;
        if (m_clearQueue)
        {
            std::scoped_lock _(queueLock);
            queue.clear();
            if (!AsyncSort.Current())
                m_clearQueue = false;
        }
        if (m_clearCache)
        {
            if (!AsyncSort.Current())
            {
                namespaces.clear();
                rootObjects.clear();
                m_clearCache = false;
            }
        }
        auto& cache = (isNamespace ? namespaces : rootObjects)[index];
        if (std::scoped_lock _(cache.Lock); cache.Objects.size() != entries.size())
        {
            cache.Objects = entries;
            cache.Reset = true;
            cache.Ready = false;
        }
        if (m_clearCache || m_clearQueue)
            return { cache.Objects, cache.Lock, !cache.Ready };
        if (cache.Reset || cache.Sort != Sort || cache.Invert != SortInvert || cache.Flatten != Flatten.has_value())
        {
            cache.Ready = false;

            cache.Reset = false;
            cache.Sort = Sort;
            cache.Invert = SortInvert;
            cache.Flatten = Flatten.has_value();

            std::scoped_lock _(queueLock);
            queue.emplace_back(&cache);
        }
        if (std::scoped_lock _(queueLock); !queue.empty() && !AsyncSort.Current())
        {
            AsyncSort.Run([this](Utils::Async::Context context)
            {
                for (uint32 i = 0;; ++i)
                {
                    Cache* next;
                    {
                        std::scoped_lock _(queueLock);
                        if (i >= queue.size())
                            break;
                        next = queue[i];
                        context->SetTotal(std::max<uint32>(context->Current, queue.size()));
                    }
                    if (auto& cache = *next; !cache.Ready)
                    {
                        decltype(cache.Objects) sorted;
                        {
                            std::scoped_lock _(cache.Lock);
                            sorted = cache.Objects;
                        }
                        SortList(sorted, cache.Sort, cache.Invert, cache.Flatten);
                        {
                            std::scoped_lock _(cache.Lock);
                            cache.Objects = std::move(sorted);
                            cache.Ready = true;
                        }
                    }
                    context->Increment();
                }
                context->Finish();
            });
        }
        return { cache.Objects, cache.Lock, !cache.Ready };
    }
    void ClearCache()
    {
        m_clearQueue = true;
        m_clearCache = true;
    }

    void SortList(std::vector<Data::Content::ContentObject const*>& data, ContentSort sort, bool invert, bool flatten)
    {
        static auto valueOrDefault = []<typename T>(T const* ptr, T def = { }) { return ptr ? *ptr : def; };
        switch (sort)
        {
            using Utils::Sort::ComplexSort;
            using Utils::Sort::Unsorted;
            using Utils::Sort::Lazy;
            using enum ContentSort;
            case GUID:
                if (flatten)
                    ComplexSort(data, invert, [](Data::Content::ContentObject const* object) { return *object->GetGUID(); });
                else
                    std::ranges::sort(data, [invert](auto a, auto b) { return a->Index < b->Index ^ invert; });
                break;
            case UID:
                ComplexSort(data, invert, [invert, flatten](Data::Content::ContentObject const* object) { return std::make_tuple(Unsorted(object->Type->Index, invert), flatten ? valueOrDefault(object->GetUID()) : 0, object->Index); });
                break;
            case DataID:
                ComplexSort(data, invert, [invert](Data::Content::ContentObject const* object) { return std::make_tuple(Unsorted(object->Type->Index, invert), valueOrDefault(object->GetDataID()), object->Index); });
                break;
            case Type:
                ComplexSort(data, invert, [invert](Data::Content::ContentObject const* object) { return std::make_tuple(object->Type->Index, Unsorted(Lazy([object] { return object->GetDisplayName(G::Config.ShowOriginalNames, true); }), invert), Unsorted(object->Index, invert)); });
                break;
            case Name:
                ComplexSort(data, invert, [flatten](Data::Content::ContentObject const* object) { return std::make_tuple(flatten ? object->GetFullDisplayName(G::Config.ShowOriginalNames, true) : object->GetDisplayName(G::Config.ShowOriginalNames, true), object->Index); });
                break;
            default: std::terminate();
        }
    }
    void UpdateSort()
    {
        m_clearQueue = true;
    }
    void UpdateFilter(bool delayed = false)
    {
        FilterName.clear();
        FilterGUID.reset();
        FilterUID = { -1, -1 };
        FilterDataID = { -1, -1 };
        int32 range;
        if (FilterString.empty())
            ;
        else if (std::string name; Utils::Scan::Into(FilterString, R"("{:[^"]}")", name))
            FilterName = name;
        else if (GUID guid; Utils::Scan::Into(FilterString, guid) || Utils::Scan::Into(FilterString, "guid:{}", guid))
            FilterGUID = guid;
        else if (uint32 dataID; Utils::Scan::Into(FilterString, "{}+{}", dataID, range) || Utils::Scan::Into(FilterString, "id:{}+{}", dataID, range) || Utils::Scan::Into(FilterString, "dataid:{}+{}", dataID, range))
            FilterDataID = { (uint32)std::max(0, (int32)dataID - range), dataID + range };
        else if (Utils::Scan::Into(FilterString, "{}-{}", dataID, range) || Utils::Scan::Into(FilterString, "id:{}-{}", dataID, range) || Utils::Scan::Into(FilterString, "dataid:{}-{}", dataID, range))
            FilterDataID = { dataID, range };
        else if (Utils::Scan::Into(FilterString, dataID) || Utils::Scan::Into(FilterString, "id:{}", dataID) || Utils::Scan::Into(FilterString, "dataid:{}", dataID))
            FilterDataID = { dataID, dataID };
        else if (uint32 uid; Utils::Scan::Into(FilterString, "uid:{}+{}", uid, range))
            FilterUID = { (uint32)std::max(0, (int32)uid - range), uid + range };
        else if (Utils::Scan::Into(FilterString, "uid:{}-{}", uid, range))
            FilterUID = { uid, range };
        else if (Utils::Scan::Into(FilterString, "uid:{}", uid))
            FilterUID = { uid, uid };
        else
            FilterName = FilterString;

        AsyncFilter.Run([this, delayed, filter = Data::Content::ContentFilter
        {
            .Type = FilterType,
            .NameSearch = Utils::Encoding::FromUTF8(FilterName),
            .GUIDSearch = FilterGUID,
            .UIDSearch = FilterUID.first != (uint32)-1 ? std::optional(FilterUID) : std::nullopt,
            .DataIDSearch = FilterDataID.first != (uint32)-1 ? std::optional(FilterDataID) : std::nullopt,
        }](Utils::Async::Context context) mutable
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
            while (!G::Game.Content.AreObjectsLoaded())
            {
                std::this_thread::sleep_for(50ms);
                CHECK_ASYNC;
            }

            CHECK_ASYNC;
            filter.FilteredNamespaces.resize(G::Game.Content.GetNamespaces().size(), Data::Content::ContentFilter::UNCACHED_RESULT);
            CHECK_ASYNC;
            filter.FilteredObjects.resize(G::Game.Content.GetRootedObjects().size() + G::Game.Content.GetUnrootedObjects().size(), Data::Content::ContentFilter::UNCACHED_RESULT);
            CHECK_ASYNC;
            context->SetTotal(filter.FilteredObjects.size() + filter.FilteredNamespaces.size());
            uint32 processed = 0;
            for (auto container : { G::Game.Content.GetRootedObjects(), G::Game.Content.GetUnrootedObjects() })
            {
                std::for_each(std::execution::par_unseq, container.begin(), container.end(), [&filter, context, &processed](Data::Content::ContentObject const* object)
                {
                    CHECK_SHARED_ASYNC;
                    object->MatchesFilter(filter); // caches result inside filter
                    if (static constexpr uint32 interval = 1000; !(++processed % interval)) // processed will like exhibit race conditions, but we don't really care how accurate it is
                        context->InterlockedIncrement(interval);
                });
            }
            CHECK_ASYNC;
            auto recurseNamespaces = [&filter, context, &processed](Data::Content::ContentNamespace const& ns, auto& recurseNamespaces) mutable -> void
            {
                CHECK_ASYNC;
                for (auto&& child : ns.Namespaces)
                    recurseNamespaces(*child, recurseNamespaces);
                CHECK_ASYNC;
                ns.MatchesFilter(filter); // caches result inside filter
                if (static constexpr uint32 interval = 1000; !(++processed % interval))
                    context->InterlockedIncrement(interval);
            };
            if (auto* root = G::Game.Content.GetNamespaceRoot())
                recurseNamespaces(*root, recurseNamespaces);

            CHECK_ASYNC;
            std::unique_lock _(Lock);
            ContentFilter = std::move(filter);
            if (SearchResultsReady)
                SearchResultsReady.emplace(true);
            if (Flatten)
                Flatten->clear();
            context->Finish();
        });
    }
    void UpdateSearch()
    {
        std::unique_lock _(Lock);
        SearchResultsReady.emplace(false);
        UpdateFilter(true);
    }

    void LocateObject(Data::Content::ContentObject const& object) { Locate(&object); }
    void LocateNamespace(Data::Content::ContentNamespace const& ns) { Locate(&ns); }

    void Draw() override
    {
        ProcessContext context { *this };

        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("Search", 2, ImGuiTableFlags_NoSavedSettings))
        {
            I::TableSetupColumn("Search");
            I::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthFixed);

            I::TableNextColumn();
            I::SetNextItemWidth(-FLT_MIN);
            if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &FilterString))
            {
                G::Windows::Demangle.MatchRecursively(Utils::Encoding::FromUTF8(FilterString));
                UpdateSearch();
            }
            Controls::AsyncProgressBar(AsyncFilter);

            I::TableNextColumn();
            if (I::Button(ICON_FA_GEAR))
                I::OpenPopup("ViewerConfig");

            I::SetNextWindowPos(I::GetCurrentContext()->LastItemData.Rect.GetBR(), ImGuiCond_Always, { 1, 0 });
            if (scoped::Popup("ViewerConfig"))
            {
                I::Checkbox("Auto-expand namespaces if ", &ViewerConfig.AutoExpandSearchResults);
                I::SameLine(0, 0);
                I::SetNextItemWidth(30);
                if (scoped::Disabled(!ViewerConfig.AutoExpandSearchResults))
                    I::DragInt("##AutoExpandMaxResults", (int*)&ViewerConfig.AutoExpandSearchMaxResults, 0.1f, 1, 10000000);
                if (I::IsItemHovered())
                    I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                I::SameLine(0, 0);
                I::TextUnformatted(" results or fewer");

                I::Checkbox("Auto-open singular search result ", &ViewerConfig.AutoOpenSearchResult);
                I::SameLine(0, 0);
                if (scoped::Disabled(!ViewerConfig.AutoOpenSearchResult))
                    I::Checkbox("in background tab", &ViewerConfig.AutoOpenSearchResultInBackgroundTab);

                I::Separator();

                I::Checkbox("Horizontal scrolling", &ViewerConfig.HorizontalScroll);
                if (scoped::Disabled(!ViewerConfig.HorizontalScroll))
                {
                    I::Checkbox("Auto-scroll right to hide tree indents", &ViewerConfig.HorizontalScrollAutoIndent);
                    I::Checkbox("Auto-scroll left when nothing is out of bounds", &ViewerConfig.HorizontalScrollAutoContent);
                }

                I::Separator();

                I::Checkbox("Draw tree lines", &ViewerConfig.DrawTreeLines);
                I::Checkbox("Draw separators between patches", &ViewerConfig.DrawSeparatorsBetweenReleases);
                I::SetItemTooltip("Speculative, determined by DataID and GUID desynchronizing their ordering.\nReleases before 2015 don't follow this rule.\nOnly works when sorting by DataID.");
            }
        }
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("Filter", 5, ImGuiTableFlags_NoSavedSettings))
        {
            I::TableSetupColumn("Type");
            I::TableSetupColumn("Flatten", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("Locate", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("Expand", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("Collapse", ImGuiTableColumnFlags_WidthFixed);

            I::TableNextColumn();
            I::SetNextItemWidth(-FLT_MIN);
            std::vector<Data::Content::ContentTypeInfo const*> values(1, nullptr);
            if (G::Game.Content.AreTypesLoaded())
            {
                values.append_range(Utils::Sort::ComplexSorted(G::Game.Content.GetTypes(), false, [](Data::Content::ContentTypeInfo const* type)
                {
                    return std::make_tuple(!type->GetTypeInfo().Favorite, type->Index);
                }));
            }
            if (Controls::FilteredComboBox("##Type", FilterType, values,
            {
                .MaxHeight = 500,
                .Formatter = [](Data::Content::ContentTypeInfo const* const& type) -> std::string
                {
                    if (!type)
                        return std::format("<c=#8>{0} {2}</c>", ICON_FA_FILTER, -1, "Any Type");

                    return std::format("<c=#8>{0}</c> {2}  <c=#4>#{1}</c>", ICON_FA_FILTER, type->Index, type->GetTypeInfo().Name);
                },
                .Filter = [](Data::Content::ContentTypeInfo const* const& type, auto const& filter, auto const& options)
                {
                    if (!type)
                        return filter.Filters.empty();

                    return filter.PassFilter(std::format("{}", type->Index).c_str()) || filter.PassFilter(Utils::Encoding::ToUTF8(type->GetDisplayName()).c_str());
                },
                .Draw = [](Data::Content::ContentTypeInfo const* const& type, bool selected, auto const& options)
                {
                    I::SetNextItemAllowOverlap();
                    auto const result = I::Selectable(options.Formatter(type).c_str(), selected, 0, { type ? I::GetCurrentWindow()->WorkRect.GetWidth() - I::GetFrameHeight() - I::GetStyle().ItemSpacing.x : 0, 0 });
                    if (selected && I::IsWindowAppearing())
                        I::ScrollToItem();
                    if (type)
                    {
                        auto& typeInfo = type->GetTypeInfo();
                        I::SameLine(0, I::GetStyle().ItemSpacing.x);
                        if (scoped::WithStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0)))
                        if (I::Selectable(std::format("<c=#{}>" ICON_FA_STAR "</c>##{}", typeInfo.Favorite ? "F" : "4", type->Index).c_str(), false, 0, { I::GetFrameHeight(), 0 }))
                            typeInfo.Favorite ^= true;
                    }
                    return result;
                },
            }))
                UpdateFilter();

            I::TableNextColumn();
            if (I::Button(std::format("<c=#{}>{}</c> <c=#{}>{}</c>", Flatten ? "4" : "F", ICON_FA_FOLDER_TREE, Flatten ? "F" : "4", ICON_FA_LIST).c_str()))
            {
                if (Flatten)
                    Flatten.reset();
                else
                    Flatten.emplace();
            }
            I::SetItemTooltip("Flatten the Content Tree");

            I::TableNextColumn();
            auto const viewer = dynamic_cast<ContentViewer*>(G::UI.GetCurrentViewer());
            if (scoped::Disabled(!viewer))
            if (I::Button(ICON_FA_FOLDER_MAGNIFYING_GLASS))
                LocateObject(viewer->Content);
            I::SetItemTooltip("Locate:\n%s", viewer ? viewer->Title().c_str() : "<no content selected>");

            I::TableNextColumn();
            if (scoped::Disabled(Flatten.has_value()))
            if (I::Button(ICON_FA_FOLDER_OPEN))
                context.ExpandAll = true;
            I::SetItemTooltip("Expand All Namespaces");

            I::TableNextColumn();
            if (scoped::Disabled(Flatten.has_value()))
            if (I::Button(ICON_FA_FOLDER_CLOSED))
                context.CollapseAll = true;
            I::SetItemTooltip("Collapse All Namespaces");
        }

        ::ImGuiWindow* window = nullptr;
        ImGuiTable* table = nullptr;
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 }))
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemInnerSpacing, { 0, I::GetStyle().ItemInnerSpacing.y }))
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, { I::GetStyle().FramePadding.x, 0 }))
        if (scoped::WithStyleVar(ImGuiStyleVar_IndentSpacing, 16))
        if (scoped::Table("Table", 7, ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable, { 0, ViewerConfig.HorizontalScroll ? -I::GetStyle().ScrollbarSize : 0 }))
        {
            window = I::GetCurrentWindow();
            table = I::GetCurrentTable();
            if (ViewerConfig.HorizontalScroll && I::GetIO().KeyShift && I::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            {
                I::SetKeyOwner(ImGuiKey_MouseWheelX, window->ID, ImGuiInputFlags_LockThisFrame);
                float max_step = table->Columns[0].ClipRect.GetWidth() * 0.67f;
                float scroll_step = ImTrunc(ImMin(10 * window->FontRefSize, max_step));
                ScrollTargetX = ScrollExpectedX - I::GetIO().MouseWheel * scroll_step;
            }

            I::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide, 0, (ImGuiID)ContentSort::Name);
            I::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40, (ImGuiID)ContentSort::Type);
            I::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 40);
            I::TableSetupColumn("Data ID", ImGuiTableColumnFlags_WidthFixed, 40, (ImGuiID)ContentSort::DataID);
            I::TableSetupColumn("UID", ImGuiTableColumnFlags_WidthFixed, 40, (ImGuiID)ContentSort::UID);
            I::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 40, (ImGuiID)ContentSort::GUID);
            I::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 40);
            I::TableSetupScrollFreeze(0, 1);
            auto const pos = I::GetCursorScreenPos();
            auto const avail = I::GetContentRegionAvail();
            I::TableHeadersRow();
            if (scoped::WithCursorScreenPos(pos.x, I::GetCursorScreenPos().y - I::TableGetHeaderRowHeight()))
            if (scoped::TableBackgroundChannel())
            {
                I::Dummy({ avail.x, I::TableGetHeaderRowHeight() });
                Controls::AsyncProgressBar(AsyncSort);
            }

            if (auto specs = I::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0)
            {
                Sort = (ContentSort)specs->Specs[0].ColumnUserID;
                SortInvert = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                specs->SpecsDirty = false;
                UpdateSort();
            }

            std::shared_lock __(Lock);

            // Virtualizing tree

            if (G::Game.Content.AreObjectsLoaded())
            {
                I::GetCurrentWindow()->SkipItems = false; // Workaround for bug with Expand/Collapse buttons not working if the last column is hidden

                if (SearchResultsReady && *SearchResultsReady)
                {
                    SearchResultsReady.reset();
                    if (ContentFilter.IsFilteringObjects())
                    {
                        auto const filteredObjects = ContentFilter.GetFilteredObjectsCount();
                        if (ViewerConfig.AutoOpenSearchResult && filteredObjects == 1)
                            context.OpenObjectButton = ViewerConfig.AutoOpenSearchResultInBackgroundTab ? ImGuiButtonFlags_MouseButtonMiddle : ImGuiButtonFlags_MouseButtonLeft;
                        if (ViewerConfig.AutoExpandSearchResults && filteredObjects >= 1 && filteredObjects <= ViewerConfig.AutoExpandSearchMaxResults)
                            context.ExpandAll = true;
                    }
                }

                if (ViewerConfig.HorizontalScroll && ScrollX)
                    I::Indent(-ScrollX);

                if (Flatten)
                {
                    if (Flatten->empty())
                    {
                        Flatten->reserve(ContentFilter.IsFilteringObjects() ? ContentFilter.GetFilteredObjectsCount() : G::Game.Content.GetObjects().size());
                        Flatten->assign_range(G::Game.Content.GetObjects() | std::views::filter([this](auto const& object) { return object->MatchesFilter(ContentFilter); }));
                    }
                    context.Draw([&, sorted = GetSortedContentObjects(true, -1, *Flatten)] { ProcessEntries(sorted, context, -1); });
                }
                else
                    context.Draw([&] { ProcessNamespace(*G::Game.Content.GetNamespaceRoot(), context, 0); });

                if (ViewerConfig.HorizontalScroll && ScrollX)
                    I::Unindent(-ScrollX);
            }
        }
        if (context.MinDrawnIndent)
            *context.MinDrawnIndent += ScrollX;

        if (ViewerConfig.HorizontalScroll)
        {
            auto bb = I::GetWindowScrollbarRect(window, ImGuiAxis_X);
            bb.Max.y = bb.Min.y + I::GetStyle().ScrollbarSize;
            auto const size_visible = table->Columns[0].WorkMaxX - table->Columns[0].WorkMinX;
            auto const size_contents = ScrollX + table->Columns[0].ContentMaxXUnfrozen - table->Columns[0].WorkMinX;
            auto const size_padded_contents = ScrollX + std::max(size_visible, table->Columns[0].ContentMaxXUnfrozen - table->Columns[0].WorkMinX);

            ScrollExpectedX = ScrollTargetX = ImRound64(ImClamp(ScrollTargetX, 0.0f, size_padded_contents - size_visible));
            auto const smooth = ScrollExpectedX + (ScrollX - ScrollExpectedX) * expf(-20 * ImGui::GetIO().DeltaTime);
            ScrollX = fabsf(smooth - ScrollExpectedX) > 0.5f ? smooth : ScrollExpectedX;

            ImS64 scroll = ScrollX;
            if (I::ScrollbarEx(bb, I::GetWindowScrollbarID(window, ImGuiAxis_X), ImGuiAxis_X, &scroll, size_visible, size_padded_contents, ImDrawFlags_RoundCornersBottom))
                ScrollX = ScrollExpectedX = ScrollTargetX = scroll;

            if (ViewerConfig.HorizontalScrollAutoContent && ScrollExpectedX > size_contents - size_visible)
                ScrollTargetX = std::max(0.0f, size_contents - size_visible);
            if (ViewerConfig.HorizontalScrollAutoIndent && context.MinDrawnIndent && ScrollExpectedX <= PrevMinDrawnIndent)
                ScrollTargetX = std::max(0.0f, *context.MinDrawnIndent);

            PrevMinDrawnIndent = context.MinDrawnIndent.value_or(0);
        }
    }

private:
    bool m_clearQueue = false;
    bool m_clearCache = false;

    using Item = std::variant<Data::Content::ContentNamespace const*, Data::Content::ContentObject const*>;

    struct ProcessContext
    {
        ContentListViewer& Viewer;

        bool ExpandAll = false;
        bool CollapseAll = false;
        ImGuiButtonFlags_ OpenObjectButton = ImGuiButtonFlags_None;
        std::optional<Item> Locate;
        Data::Content::ContentObject const* PreviousObject = nullptr;
        std::optional<float> MinDrawnIndent;

        ProcessContext(ContentListViewer& viewer) : Viewer(viewer) { }

        void Draw(std::function<void()>&& process)
        {
            if (Viewer.m_locate)
                Locate = std::exchange(Viewer.m_locate, std::nullopt);

            process(); // Dry run to count elements
            while (Step())
                process();
        }

        bool IsRealItem(bool& open, bool canOpen, Item item, int parentIndex, ImGuiDockNodeFlags flags)
        {
            m_currentItem = item;
            m_currentIndex = m_virtualIndex++;
            m_currentParentIndex = parentIndex;

            if (canOpen)
            {
                if (ExpandAll || LocateContainedIn(item)) I::SetNextItemOpen(true);
                if (CollapseAll) I::SetNextItemOpen(false);
            }

            // Real item
            if (m_currentIndex >= m_clipper.DisplayStart && m_currentIndex < m_clipper.DisplayEnd)
                return true;

            // Virtual item
            auto const window = I::GetCurrentWindow();
            auto const table = I::GetCurrentTable();
            auto const id = window->GetID(std::visit([](auto const* item) -> void const* { return item; }, m_currentItem));

            if (!m_drawing && m_focusedParentIndex == -1 && I::GetFocusID() == id)
                m_focusedParentIndex = m_currentParentIndex;

            if (!m_drawing && Locate && *Locate == m_currentItem)
                m_locateIndex = m_currentIndex;

            if (m_drawing && ViewerConfig.DrawTreeLines && I::GetCursorScreenPos().y + I::GetFrameHeight() >= window->ClipRect.Max.y)
                I::TreeNodeDrawLineToChildNode({ table->Columns[0].WorkMinX + window->DC.Indent.x - table->HostIndentX, I::GetCursorScreenPos().y + I::GetFrameHeight() });

            if ((open = canOpen && (I::TreeNodeUpdateNextOpen(id, 0) || CollapseAll)))
            {
                if (m_drawing && ViewerConfig.DrawTreeLines && I::GetCursorScreenPos().y <= window->ClipRect.Min.y)
                {
                    ImVec2 const pos { table->Columns[0].WorkMinX + window->DC.Indent.x - table->HostIndentX, I::GetCursorScreenPos().y - I::GetFrameHeight() };
                    I::GetCurrentContext()->LastItemData.ID = id;
                    I::GetCurrentContext()->LastItemData.NavRect = { pos, pos + I::GetFrameSquare() };
                    I::TreeNodeStoreStackData(flags, pos.x);
                }

                I::TreePushOverrideID(id);
            }
            return false;
        }
        void BeginItem()
        {
            auto const window = I::GetCurrentWindow();
            if (window->ClipRect.Overlaps({ I::GetCursorScreenPos(), I::GetCursorScreenPos() + ImVec2(window->ClipRect.GetWidth(), I::GetFrameHeight()) }))
                MinDrawnIndent = std::min(MinDrawnIndent.value_or(FLT_MAX), window->DC.Indent.x);
        }
        void CommitItem() const
        {
            if (WantsToNavigateLeft() && m_currentIndex == m_focusedParentIndex)
                I::NavMoveRequestResolveWithLastItem(&I::GetCurrentContext()->NavMoveResultLocal);

            if (Locate && *Locate == m_currentItem)
            {
                I::ScrollToItem();
                I::FocusItem();
            }

            auto const hovered = std::visit(Utils::Visitor::Overloaded
            {
                [](Data::Content::ContentObject const* object) { G::UI.Hovered.Object.SetLastItem(object); return G::UI.Hovered.Object.IsNotLastItem(object); },
                [](Data::Content::ContentNamespace const* ns) { G::UI.Hovered.Namespace.SetLastItem(ns); return G::UI.Hovered.Namespace.IsNotLastItem(ns); },
            }, m_currentItem);
            if (auto const alpha = std::max(Viewer.ShouldHighlight(m_currentItem), hovered ? 0.15f : 0.0f))
                I::TableSetBgColor(ImGuiTableBgTarget_RowBg1, I::GetColorU32(0x80FFFFFF, alpha));
        }

        int GetCurrentIndex() const { return m_currentIndex; }
        bool CanSkip() const
        {
            // Optimization: skip traversing the tree when all required items were drawn
            if (m_canSkip && m_virtualIndex >= m_clipper.DisplayEnd)
                return true;

            // Optimization: don't traverse the tree when clipper is only measuring the first item's height
            if (m_measuring)
                return true;

            return false;
        }

    private:
        bool m_drawing = false;
        bool m_measuring = false;
        bool m_canSkip = false;

        Item m_currentItem;
        int m_currentIndex = -1;
        int m_currentParentIndex = -1;
        int m_virtualIndex = 0;
        ImGuiListClipper m_clipper;
        bool Step()
        {
            if (!m_drawing)
            {
                m_drawing = true;
                m_canSkip = !ViewerConfig.DrawTreeLines;
                m_clipper.Begin(m_virtualIndex, I::GetFrameHeight());
                if (WantsToNavigateLeft() && m_focusedParentIndex >= 0)
                    m_clipper.IncludeItemByIndex(m_focusedParentIndex);
                if (Locate && m_locateIndex >= 0)
                    m_clipper.IncludeItemByIndex(m_locateIndex);
            }
            bool const result = m_clipper.Step();
            m_measuring = m_clipper.DisplayStart == 0 && m_clipper.DisplayEnd == 1;
            m_virtualIndex = 0;
            return result;
        }

        int m_focusedParentIndex = -1;
        auto WantsToNavigateLeft() const
        {
            auto& g = *I::GetCurrentContext();
            return m_drawing && g.NavMoveScoringItems && g.NavWindow && g.NavWindow->RootWindowForNav == g.CurrentWindow->RootWindowForNav && g.NavMoveClipDir == ImGuiDir_Left;
        }

        int m_locateIndex = -1;
        bool LocateContainedIn(Item const& item) const
        {
            return Locate && std::visit(Utils::Visitor::Overloaded
            {
                [](Data::Content::ContentObject const* object, Data::Content::ContentNamespace const* locate) { return false; },
                [](auto const* parent, auto const* locate) { return parent->Contains(*locate); }
            }, item, *Locate);
        }
    };

    std::optional<Item> m_locate;
    void Locate(Item item)
    {
        m_locate = item;
        Highlight(item);
    }

    std::optional<std::tuple<Item, Time::Point>> m_highlight;
    void Highlight(Item item, Time::Duration duration = 2s) { m_highlight.emplace(item, Time::Now() + duration); }
    float ShouldHighlight(Item item)
    {
        if (!m_highlight)
            return 0;

        auto&& [highlightItem, highlightUntil] = *m_highlight;
        if (highlightItem != item)
            return 0;

        auto const now = Time::Now();
        if (now >= highlightUntil)
        {
            m_highlight.reset();
            return 0;
        }

        return std::clamp(Time::ToMs(Time::Between(now, highlightUntil)).count() / 1000.0f, 0.0f, 1.0f);
    }

    void ProcessNamespace(Data::Content::ContentNamespace const& ns, ProcessContext& context, int parentIndex)
    {
        if (!ns.MatchesFilter(ContentFilter))
            return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NavLeftJumpsToParent;
        if (ViewerConfig.DrawTreeLines)
            flags |= ImGuiTreeNodeFlags_DrawLinesToNodes;

        bool open;
        if (context.IsRealItem(open, true, &ns, parentIndex, flags))
        {
            static constexpr char const* DOMAINS[] { "System", "Game", "Common", "Template", "World", "Continent", "Region", "Map", "Section", "Tool" };

            I::TableNextRow();

            I::TableNextColumn();
            I::SetNextItemAllowOverlap();
            context.BeginItem();
            open = I::TreeNodeEx(&ns, flags, ICON_FA_FOLDER " %s", Utils::Encoding::ToUTF8(ns.GetDisplayName(G::Config.ShowOriginalNames)).c_str());
            context.CommitItem();

            if (I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft) && I::GetIO().KeyCtrl)
                G::Windows::Demangle.OpenBruteforceUI(std::format(L"{}.", ns.GetFullDisplayName()), nullptr, true);

            if (scoped::PopupContextItem())
            if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
            {
                I::Text("Full Name: %s", Utils::Encoding::ToUTF8(ns.GetFullName()).c_str());
                I::InputTextUTF8("Name", G::Config.ContentNamespaceNames, ns.GetFullName(), ns.Name);

                Controls::CopyButton("Mangled Name", ns.Name);
                I::SameLine();
                Controls::CopyButton("Full Mangled Name", ns.GetFullName());

                Controls::CopyButton("Name", ns.GetDisplayName(G::Config.ShowOriginalNames, true));
                I::SameLine();
                Controls::CopyButton("Full Name", ns.GetFullDisplayName(G::Config.ShowOriginalNames, true));

                I::AlignTextToFramePadding();
                I::TextUnformatted("Bruteforce Demangle Name:");
                I::SameLine();
                if (ns.Parent && I::Button("This"))
                    G::Windows::Demangle.OpenBruteforceUI(std::format(L"{}.", ns.Parent->GetFullDisplayName(false, true)), nullptr, true, false, true);
                I::SameLine();
                if (I::Button("Children"))
                    G::Windows::Demangle.OpenBruteforceUI(std::format(L"{}.", ns.GetFullDisplayName(false, true)), nullptr, true, false, true);
                I::SameLine();
                if (I::Button("Recursively"))
                    G::Windows::Demangle.OpenBruteforceUI(std::format(L"{}.", ns.GetFullDisplayName(false, true)), &ns, true, false, true);
                if (G::Windows::Demangle.CanSkipRecursiveBruteforceTo(ns))
                    if (I::SameLine(); I::Button(ICON_FA_FORWARD_FAST " Skip to This"))
                        G::Windows::Demangle.SkipRecursiveBruteforceTo(ns);
            }

            if (open && ContentFilter && (ContentFilter.IsFilteringNamespaces() || ContentFilter.IsFilteringObjects()))
            {
                I::SameLine();
                if (std::ranges::any_of(ns.Namespaces, [this](auto const& child) { return !ContentFilter.FilteredNamespaces[child->Index]; }) ||
                    std::ranges::any_of(ns.Entries, [this](auto const& child) { return !ContentFilter.FilteredObjects[child->Index]; }))
                {
                    if (I::Button("<c=#4>" ICON_FA_FILTER_SLASH "</c>"))
                    {
                        auto const recurse = I::GetIO().KeyShift;
                        auto process = Utils::Visitor::Overloaded
                        {
                            [this, recurse](Data::Content::ContentNamespace const& parent, auto& process) -> void
                            {
                                for (auto const& child : parent.Namespaces)
                                {
                                    ContentFilter.FilteredNamespaces[child->Index] = true;
                                    if (recurse)
                                        process(*child, process);
                                }
                                for (auto const& child : parent.Entries)
                                {
                                    ContentFilter.FilteredObjects[child->Index] = true;
                                    if (recurse)
                                        process(*child, process);
                                }
                            },
                            [this, recurse](Data::Content::ContentObject const& parent, auto& process) -> void
                            {
                                for (auto const& child : parent.Entries)
                                {
                                    ContentFilter.FilteredObjects[child->Index] = true;
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

        I::GetCurrentContext()->NextItemData.ClearFlags();

        if (open)
        {
            auto pop = gsl::finally(&I::TreePop);

            if (context.CanSkip())
                return;

            auto const index = context.GetCurrentIndex();
            for (auto const& child : ns.Namespaces)
            {
                ProcessNamespace(*child, context, index);

                if (context.CanSkip())
                    return;
            }
            context.PreviousObject = nullptr;
            ProcessEntries(GetSortedContentObjects(true, ns.Index, ns.Entries), context, index);
        }
    }

    void ProcessEntries(SortedContentObjects const& entries, ProcessContext& context, int parentIndex)
    {
        if (entries.SortPending)
            I::PushStyleVar(ImGuiStyleVar_Alpha, 0.375f + 0.125f * std::cosf(I::GetTime() * 10));

        std::scoped_lock _(entries.Lock);

        for (auto* child : entries.Objects)
        {
            auto const& entry = *child;
            if (!entry.MatchesFilter(ContentFilter))
                continue;

            bool const canOpen = !entry.Entries.empty();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NavLeftJumpsToParent;
            if (ViewerConfig.DrawTreeLines)
                flags |= ImGuiTreeNodeFlags_DrawLinesToNodes;
            if (canOpen)
                flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            else
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            bool open = false;
            if (context.IsRealItem(open, canOpen, &entry, parentIndex, flags))
            {
                if (auto const* currentViewer = G::UI.GetCurrentViewer<ContentViewer>())
                    if (currentViewer->IsCurrent(entry))
                        flags |= ImGuiTreeNodeFlags_Selected;

                entry.Finalize();
                I::TableNextRow();

                I::TableNextColumn();
                I::SetNextItemAllowOverlap();
                auto const cursorRowStart = I::GetCursorScreenPos();
                context.BeginItem();
                open = I::TreeNodeEx(&entry, flags, "") && canOpen;
                context.CommitItem();
                auto const rect = I::GetCurrentContext()->LastItemData.Rect;

                if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle) | context.OpenObjectButton)
                    ContentViewer::Open(entry, { .MouseButton = button });

                if (scoped::PopupContextItem())
                if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
                {
                    I::Text("Full Name: %s", Utils::Encoding::ToUTF8(entry.GetFullName()).c_str());
                    if (I::InputTextUTF8("Name", G::Config.ContentObjectNames, *entry.GetGUID(), entry.GetName() && entry.GetName()->Name && *entry.GetName()->Name ? *entry.GetName()->Name : entry.GetDisplayName()))
                        ClearCache();

                    Controls::CopyButton("GUID", entry.GetGUID() ? *entry.GetGUID() : GUID::Empty, entry.GetGUID());
                    I::SameLine();
                    Controls::CopyButton("UID", entry.GetUID() ? *entry.GetUID() : 0, entry.GetUID());
                    I::SameLine();
                    Controls::CopyButton("Data ID", entry.GetDataID() ? *entry.GetDataID() : 0, entry.GetDataID());

                    Controls::CopyButton("Type Index", entry.Type->Index);
                    I::SameLine();
                    Controls::CopyButton("Type Name", entry.Type->GetDisplayName());
                    I::SameLine();
                    if (I::Button(ICON_FA_FILTER))
                    {
                        FilterType = entry.Type;
                        UpdateFilter();
                    }

                    Controls::CopyButton("Mangled Name", entry.GetName() ? *entry.GetName()->Name : L"", entry.GetName());
                    I::SameLine();
                    Controls::CopyButton("Full Mangled Name", entry.GetFullName(), entry.GetName());

                    Controls::CopyButton("Name", I::StripMarkup(entry.GetDisplayName(false, true)));
                    I::SameLine();
                    Controls::CopyButton("Full Name", I::StripMarkup(entry.GetFullDisplayName(false, true)));

                    if (entry.Namespace && I::Button("Bruteforce Demangle Name"))
                        G::Windows::Demangle.OpenBruteforceUI(std::format(L"{}.", entry.Namespace->GetFullDisplayName(false, true)), nullptr, true, true, false);

                    if (I::Button("Search for Content References"))
                        G::Windows::ContentSearch.SearchForSymbolValue("Content*", (Data::Content::TypeInfo::Condition::ValueType)entry.Data.data());
                }

                I::SameLine(0, 0);
                if (open && ContentFilter && ContentFilter.IsFilteringObjects())
                {
                    if (std::ranges::any_of(entry.Entries, [this](auto const& child) { return !ContentFilter.FilteredObjects[child->Index]; }))
                    {
                        if (I::Button("<c=#4>" ICON_FA_FILTER_SLASH "</c>"))
                            for (auto const& child : entry.Entries)
                                ContentFilter.FilteredObjects[child->Index] = true;
                        if (scoped::ItemTooltip())
                            I::TextUnformatted("Show all children");
                        I::SameLine();
                    }
                }
                if (auto const icon = entry.GetIcon(); icon && !entry.Type->GetTypeInfo().DisplayFormat.contains("@icon"))
                    if (Controls::Texture(icon, { .Size = { 0, I::GetFrameHeight() } }))
                        I::SameLine(0, 0);
                I::Text("%s", Utils::Encoding::ToUTF8(Flatten ? entry.GetFullDisplayName(G::Config.ShowOriginalNames) : entry.GetDisplayName(G::Config.ShowOriginalNames)).c_str());

                I::TableNextColumn(); I::TextUnformatted(Utils::Encoding::ToUTF8(entry.Type->GetDisplayName()).c_str());
                I::TableNextColumn(); I::Text("%u", entry.Data.size());
                I::TableNextColumn(); if (auto* id = entry.GetDataID()) I::Text("%i", *id);
                I::TableNextColumn(); if (auto* uid = entry.GetUID()) I::Text("%i", *uid);
                I::TableNextColumn(); if (auto* guid = entry.GetGUID()) { I::TextUnformatted(std::format("{}", *guid).c_str()); I::SetItemTooltip(std::format("{}", *guid).c_str()); }
                I::TableNextColumn(); if (!entry.IncomingReferences.empty()) I::TextColored({ 0, 1, 0, 1 }, ICON_FA_ARROW_LEFT "%u", (uint32)entry.IncomingReferences.size());

                if (auto const prev = std::exchange(context.PreviousObject, &entry))
                {
                    if (ViewerConfig.DrawSeparatorsBetweenReleases && Sort == ContentSort::DataID && prev->Type == entry.Type)
                        if (auto const guidPrev = prev->GetGUID(), guidCurrent = entry.GetGUID(); guidPrev && guidCurrent && *guidPrev > *guidCurrent != SortInvert)
                            if (scoped::TableBackgroundChannel())
                                I::GetWindowDrawList()->AddLine({ cursorRowStart.x + I::GetFontSize() + I::GetStyle().FramePadding.x * 2, rect.GetTL().y }, rect.GetTR(), I::GetColorU32(ImGuiCol_SeparatorActive));
                }
            }

            I::GetCurrentContext()->NextItemData.ClearFlags();

            if (open)
            {
                auto pop = gsl::finally(&I::TreePop);

                if (context.CanSkip())
                    continue;

                context.PreviousObject = nullptr;
                ProcessEntries(GetSortedContentObjects(false, entry.Index, entry.Entries), context, context.GetCurrentIndex());
            }

            if (context.CanSkip())
                break;
        }

        if (entries.SortPending)
            I::PopStyleVar();
    }
};

}
