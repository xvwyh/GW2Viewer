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

struct ContentListViewer : ListViewer<ContentListViewer, { ICON_FA_FOLDER_TREE " Content", "Content", Category::ListViewer }>
{
    ContentListViewer(uint32 id, bool newTab) : Base(id, newTab) { }

    std::shared_mutex Lock;
    Data::Content::ContentFilter ContentFilter;
    Utils::Async::Scheduler AsyncFilter { true };

    Data::Content::ContentTypeInfo const* FilterType { };
    std::string FilterString;
    std::string FilterName;
    std::optional<GUID> FilterGUID;
    std::pair<uint32, uint32> FilterUID { -1, -1 };
    std::pair<uint32, uint32> FilterDataID { -1, -1 };
    enum class ContentSort { GUID, UID, DataID, Type, Name } Sort { ContentSort::GUID };
    bool SortInvert { };

    auto GetSortedContentObjects(bool isNamespace, uint32 index, std::list<std::unique_ptr<Data::Content::ContentObject>> const& entries)
    {
        auto timeout = Time::FrameStart + 10ms;
        struct Cache
        {
            ContentSort Sort;
            bool Invert;
            std::vector<uint32> Objects;
        };
        static std::unordered_map<uint32, Cache> namespaces, rootObjects;
        if (m_clearCache)
        {
            namespaces.clear();
            rootObjects.clear();
            m_clearCache = false;
        }
        auto& cache = (isNamespace ? namespaces : rootObjects)[index];
        bool reset = false;
        if (cache.Objects.size() != entries.size())
        {
            cache.Objects.assign_range(entries | std::views::transform([](auto const& ptr) { return ptr->Index; }));
            reset = true;
        }
        if ((reset || cache.Sort != Sort || cache.Invert != SortInvert) && Time::PreciseNow() < timeout)
            SortList(cache.Objects, (cache.Sort = Sort), (cache.Invert = SortInvert));
        return cache.Objects | std::views::transform([](uint32 index) { return G::Game.Content.GetByIndex(index); });
    }
    void ClearCache() { m_clearCache = true; }

    void SortList(std::vector<uint32>& data, ContentSort sort, bool invert)
    {
        #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
        switch (sort)
        {
            using Utils::Sort::ComplexSort;
            using enum ContentSort;
            case GUID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a < b ^ invert; });
                break;
            case UID:
                ComplexSort(data, invert, [](uint32 id) { return G::Game.Content.GetByIndex(id); }, [invert](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    COMPARE((invert ? bInfo : aInfo)->Type->Index, (invert ? aInfo : bInfo)->Type->Index);
                    COMPARE(a, b);
                    return false;
                });
                break;
            case DataID:
                ComplexSort(data, invert, [](uint32 id) { return G::Game.Content.GetByIndex(id); }, [invert](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
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
                ComplexSort(data, invert, [](uint32 id) { return G::Game.Content.GetByIndex(id); }, [this, invert](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    COMPARE(aInfo->Type->Index, bInfo->Type->Index);
                    COMPARE((invert ? bInfo : aInfo)->GetDisplayName(G::Config.ShowOriginalNames, true), (invert ? aInfo : bInfo)->GetDisplayName(G::Config.ShowOriginalNames, true));
                    //if (_wcsicmp(aInfo->GetDisplayName(G::Config.ShowOriginalNames, true).c_str(), bInfo->GetDisplayName(G::Config.ShowOriginalNames, true).c_str()) < 0) return true;
                    COMPARE(a, b);
                    return false;
                });
                break;
            case Name:
                ComplexSort(data, invert, [](uint32 id) { return G::Game.Content.GetByIndex(id); }, [this](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    COMPARE(aInfo->GetDisplayName(G::Config.ShowOriginalNames, true), bInfo->GetDisplayName(G::Config.ShowOriginalNames, true));
                    //if (_wcsicmp(aInfo->GetDisplayName(G::Config.ShowOriginalNames, true).c_str(), bInfo->GetDisplayName(G::Config.ShowOriginalNames, true).c_str()) < 0) return true;
                    COMPARE(a, b);
                    return false;
                });
                break;
            default: std::terminate();
        }
        #undef COMPARE
    }
    void UpdateSort() { }
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
            for (auto* container : { &G::Game.Content.GetRootedObjects(), &G::Game.Content.GetUnrootedObjects() })
            {
                std::for_each(std::execution::par_unseq, container->begin(), container->end(), [&filter, context, &processed](Data::Content::ContentObject* object)
                {
                    CHECK_SHARED_ASYNC;
                    object->MatchesFilter(filter); // caches result inside filter
                    if (static constexpr uint32 interval = 1000; !(++processed % interval)) // processed will like exhibit race conditions, but we don't really care how accurate it is
                        context->InterlockedIncrement(interval);
                });
            }
            CHECK_ASYNC;
            auto recurseNamespaces = [&filter, context, &processed](Data::Content::ContentNamespace& ns, auto& recurseNamespaces) mutable -> void
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
            context->Finish();
        });
    }

    void Draw() override
    {
        ProcessContext context;
        I::SetNextItemWidth(-FLT_MIN);
        if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &FilterString))
        {
            G::Windows::Demangle.MatchRecursively(Utils::Encoding::FromUTF8(FilterString));
            UpdateFilter(true);
        }
        Controls::AsyncProgressBar(AsyncFilter);
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("Filter", 3, ImGuiTableFlags_NoSavedSettings))
        {
            I::TableSetupColumn("Type");
            I::TableSetupColumn("Expand", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("Collapse", ImGuiTableColumnFlags_WidthFixed);
            I::TableNextColumn();
            I::SetNextItemWidth(-FLT_MIN);
            std::vector<Data::Content::ContentTypeInfo const*> values(1, nullptr);
            if (G::Game.Content.AreTypesLoaded())
                values.append_range(G::Game.Content.GetTypes() | std::views::transform([](auto const& ptr) { return ptr.get(); }));
            if (Controls::FilteredComboBox("##Type", FilterType, values,
            {
                .MaxHeight = 500,
                .Formatter = [](auto const& type) -> std::string
                {
                    if (!type)
                        return std::format("<c=#8>{0} {2}</c>", ICON_FA_FILTER, -1, "Any Type");

                    auto const itr = G::Config.TypeInfo.find(type->Index);
                    return std::format("<c=#8>{0}</c> {2}  <c=#4>#{1}</c>", ICON_FA_FILTER, type->Index, itr != G::Config.TypeInfo.end() && !itr->second.Name.empty() ? itr->second.Name : "");
                },
                .Filter = [](auto const& type, auto const& filter, auto const& options)
                {
                    if (!type)
                        return filter.Filters.empty();

                    return filter.PassFilter(std::format("{}", type->Index).c_str()) || filter.PassFilter(Utils::Encoding::ToUTF8(type->GetDisplayName()).c_str());
                },
            }))
                UpdateFilter();
            if (I::TableNextColumn(); I::Button(ICON_FA_FOLDER_OPEN))
                context.ExpandAll = true;
            I::SetItemTooltip("Expand All Namespaces");
            if (I::TableNextColumn(); I::Button(ICON_FA_FOLDER_CLOSED))
                context.CollapseAll = true;
            I::SetItemTooltip("Collapse All Namespaces");
        }
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemInnerSpacing, { 0, I::GetStyle().ItemInnerSpacing.y }))
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
                if (auto* root = G::Game.Content.GetNamespaceRoot())
                    ProcessNamespace(*root, context, 0); // Dry run to count elements
                context.clipper.Begin(context.VirtualIndex, I::GetFrameHeight());
                if (context.navigateLeft() && context.focusedParentNamespaceIndex >= 0)
                    context.clipper.IncludeItemByIndex(context.focusedParentNamespaceIndex);
                while (context.clipper.Step())
                {
                    context.VirtualIndex = 0;
                    if (auto* root = G::Game.Content.GetNamespaceRoot())
                        ProcessNamespace(*root, context, 0);
                }
            }
        }
    }

private:
    bool m_clearCache;

    struct ProcessContext
    {
        bool ExpandAll = false;
        bool CollapseAll = false;
        int VirtualIndex = 0;

        int focusedParentNamespaceIndex = -1;
        ImGuiListClipper clipper;
        auto navigateLeft()
        {
            auto& g = *I::GetCurrentContext();
            return clipper.ItemsCount && g.NavMoveScoringItems && g.NavWindow && g.NavWindow->RootWindowForNav == g.CurrentWindow->RootWindowForNav && g.NavMoveClipDir == ImGuiDir_Left;
        };
        void storeFocusedParentInfo(int parentIndex, std::optional<ImGuiID> id = { })
        {
            if (!clipper.ItemsCount && focusedParentNamespaceIndex == -1 && id ? I::GetFocusID() == *id : I::IsItemFocused())
                focusedParentNamespaceIndex = parentIndex;
        };
    };

    void ProcessNamespace(Data::Content::ContentNamespace& ns, ProcessContext& context, int parentNamespaceIndex)
    {
        if (!ns.MatchesFilter(ContentFilter))
            return;

        if (context.ExpandAll) I::SetNextItemOpen(true);
        if (context.CollapseAll) I::SetNextItemOpen(false);

        bool open;
        int namespaceIndex;
        if (auto const index = namespaceIndex = context.VirtualIndex++; index >= context.clipper.DisplayStart && index < context.clipper.DisplayEnd || context.navigateLeft() && namespaceIndex == context.focusedParentNamespaceIndex)
        {
            static constexpr char const* DOMAINS[] { "System", "Game", "Common", "Template", "World", "Continent", "Region", "Map", "Section", "Tool" };
            I::TableNextRow();
            I::TableNextColumn(); I::SetNextItemAllowOverlap(); open = I::TreeNodeEx(&ns, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NavLeftJumpsToParent, ICON_FA_FOLDER " %s", Utils::Encoding::ToUTF8(ns.GetDisplayName(G::Config.ShowOriginalNames)).c_str());
            context.storeFocusedParentInfo(parentNamespaceIndex);
            if (context.navigateLeft() && namespaceIndex == context.focusedParentNamespaceIndex)
                I::NavMoveRequestResolveWithLastItem(&I::GetCurrentContext()->NavMoveResultLocal);
            if (I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft) && I::GetIO().KeyCtrl)
                G::Windows::Demangle.OpenBruteforceUI(std::format(L"{}.", ns.GetFullDisplayName()), nullptr, true);
            if (scoped::PopupContextItem())
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
        else
        {
            auto const id = I::GetCurrentWindow()->GetID(&ns);
            context.storeFocusedParentInfo(parentNamespaceIndex, id);
            if ((open = I::TreeNodeUpdateNextOpen(id, 0) || context.CollapseAll))
                I::TreePushOverrideID(id);
        }

        if (open)
        {
            auto pop = gsl::finally(&I::TreePop);

            // Optimization: skip traversing the tree when all required items were drawn
            if (context.clipper.ItemsCount && context.VirtualIndex >= context.clipper.DisplayEnd)
                return;

            // Optimization: don't traverse the tree when clipper is only measuring the first item's height
            if (context.clipper.DisplayStart == 0 && context.clipper.DisplayEnd == 1)
                return;

            for (auto const& child : ns.Namespaces)
            {
                ProcessNamespace(*child, context, namespaceIndex);

                // Optimization: stop traversing the tree early when all required items were drawn
                if (context.clipper.ItemsCount && context.VirtualIndex >= context.clipper.DisplayEnd)
                    return;
            }
            ProcessEntries(GetSortedContentObjects(true, ns.Index, ns.Entries), context, namespaceIndex);
        }
    }

    void ProcessEntries(auto const& entries, ProcessContext& context, int namespaceIndex)
    {
        for (auto* child : entries)
        {
            Data::Content::ContentObject& entry = *child;
            if (!entry.MatchesFilter(ContentFilter))
                continue;

            bool const hasEntries = !entry.Entries.empty();
            if (hasEntries)
            {
                if (context.ExpandAll) I::SetNextItemOpen(true);
                if (context.CollapseAll) I::SetNextItemOpen(false);
            }

            bool open = false;
            if (auto const index = context.VirtualIndex++; index >= context.clipper.DisplayStart && index < context.clipper.DisplayEnd)
            {
                auto const* currentViewer = G::UI.GetCurrentViewer<ContentViewer>();
                entry.Finalize();
                I::TableNextRow();
                I::TableNextColumn(); I::SetNextItemAllowOverlap(); open = I::TreeNodeEx(&entry, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding | (entry.Entries.empty() ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick) | (currentViewer && &currentViewer->Content == &entry ? ImGuiTreeNodeFlags_Selected : 0), "") && hasEntries;
                context.storeFocusedParentInfo(namespaceIndex);
                if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                    ContentViewer::Open(entry, { .MouseButton = button });
                if (scoped::PopupContextItem())
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
                if (auto const icon = entry.GetIcon())
                    if (Controls::Texture(icon, { .Size = { 0, I::GetFrameHeight() } }))
                        I::SameLine();
                I::Text("%s", Utils::Encoding::ToUTF8(entry.GetDisplayName(G::Config.ShowOriginalNames)).c_str());

                I::TableNextColumn(); I::TextUnformatted(Utils::Encoding::ToUTF8(entry.Type->GetDisplayName()).c_str());
                I::TableNextColumn(); I::Text("%u", entry.Data.size());
                I::TableNextColumn(); if (auto* id = entry.GetDataID()) I::Text("%i", *id);
                I::TableNextColumn(); if (auto* uid = entry.GetUID()) I::Text("%i", *uid);
                I::TableNextColumn(); if (auto* guid = entry.GetGUID()) { I::TextUnformatted(std::format("{}", *guid).c_str()); I::SetItemTooltip(std::format("{}", *guid).c_str()); }
                I::TableNextColumn(); if (!entry.IncomingReferences.empty()) I::TextColored({ 0, 1, 0, 1 }, ICON_FA_ARROW_LEFT "%u", (uint32)entry.IncomingReferences.size());
            }
            else
            {
                auto const id = I::GetCurrentWindow()->GetID(&entry);
                context.storeFocusedParentInfo(namespaceIndex, id);
                if (hasEntries && (open = I::TreeNodeUpdateNextOpen(id, 0) || context.CollapseAll))
                    I::TreePushOverrideID(id);
            }

            if (open)
            {
                auto pop = gsl::finally(&I::TreePop);

                // Optimization: skip traversing the tree when all required items were drawn
                if (context.clipper.ItemsCount && context.VirtualIndex >= context.clipper.DisplayEnd)
                    continue;

                // Optimization: don't traverse the tree when clipper is only measuring the first item's height
                if (context.clipper.DisplayStart == 0 && context.clipper.DisplayEnd == 1)
                    continue;

                ProcessEntries(GetSortedContentObjects(false, entry.Index, entry.Entries), context, namespaceIndex);
            }

            // Optimization: stop traversing the tree early when all required items were drawn
            if (context.clipper.ItemsCount && context.VirtualIndex >= context.clipper.DisplayEnd)
                break;
        }
    }
};

}
