module;
#include "UI/ImGui/ImGui.h"
#include "Utils/Async.h"

export module GW2Viewer.UI.Viewers.FileListViewer;
import GW2Viewer.Common;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Pack;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.UI.Windows.ContentSearch;
import GW2Viewer.User.ArchiveIndex;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.CRC;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.Scan;
import GW2Viewer.Utils.Sort;
import std;
import magic_enum;

using GW2Viewer::Data::Archive::File;

export namespace GW2Viewer::UI::Viewers
{

struct FileListViewer : ListViewer<FileListViewer, { ICON_FA_FILE " Files", "Files", Category::ListViewer }>
{
    FileListViewer(uint32 id, bool newTab) : Base(id, newTab) { }

    std::shared_mutex Lock;
    std::vector<File> FilteredList;
    Utils::Async::Scheduler AsyncFilter { true };

    std::optional<File> ScrollTo;

    std::string FilterString;
    std::optional<std::pair<int32, int32>> FilterID;
    uint32 FilterRange { };
    std::optional<User::ArchiveIndex::Type> FilterType;
    enum class FileSort { ID, Archive, FourCC, Type, Metadata, Manifest, Added, Changed, Size, CompressedSize, ExtraBytes, Flags, Stream, NextStream, CRC, Refs } Sort { FileSort::ID };
    bool SortInvert { };

    void SortList(Utils::Async::Context context, std::vector<File>& data, FileSort sort, bool invert)
    {
        #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
        switch (sort)
        {
            using Utils::Sort::ComplexSort;
            using enum FileSort;
            case ID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a.ID < b.ID ^ invert; });
                break;
            case Archive:
                ComplexSort(data, invert, [](File const& file) { return file.GetSourceLoadOrder(); });
                break;
            case FourCC:
                ComplexSort(data, invert, [](File const& file) { return G::ArchiveIndex[file.GetSourceKind()].GetFileMetadata(file.ID).FourCCToString(); });
                break;
            case Type:
                ComplexSort(data, invert, [](File const& file) { return magic_enum::enum_name(G::ArchiveIndex[file.GetSourceKind()].GetFileMetadata(file.ID).Type); });
                break;
            case Metadata:
                ComplexSort(data, invert, [](File const& file) { return G::ArchiveIndex[file.GetSourceKind()].GetFileMetadata(file.ID).DataToString(); });
                break;
            case Manifest:
                ComplexSort(data, invert, [](File const& file) { return std::vector { std::from_range, file.GetManifestAsset().ManifestNames }; });
                break;
            case Added:
                ComplexSort(data, invert, [](File const& file) { return G::ArchiveIndex[file.GetSourceKind()].GetFileAddedTimestamp(file.ID).Build; });
                break;
            case Changed:
                ComplexSort(data, invert, [](File const& file) { return G::ArchiveIndex[file.GetSourceKind()].GetFileChangedTimestamp(file.ID).Build; });
                break;
            case Size:
                ComplexSort(data, invert, [](File const& file) { return G::ArchiveIndex[file.GetSourceKind()].GetFile(file.ID).FileSize; });
                break;
            case CompressedSize:
                ComplexSort(data, invert, [](File const& file) { return file.GetMftEntry().alloc.size; });
                break;
            case ExtraBytes:
                ComplexSort(data, invert, [](File const& file) { return file.GetMftEntry().alloc.extraBytes; });
                break;
            case Flags:
                ComplexSort(data, invert, [](File const& file) { return file.GetMftEntry().alloc.flags; });
                break;
            case Stream:
                ComplexSort(data, invert, [](File const& file) { return file.GetMftEntry().alloc.stream; });
                break;
            case NextStream:
                ComplexSort(data, invert, [](File const& file) { return file.GetMftEntry().alloc.nextStream; });
                break;
            case CRC:
                ComplexSort(data, invert, [](File const& file) { return file.GetMftEntry().alloc.crc; });
                break;
            case Refs:
                ComplexSort(data, invert, [](File const& file) { return G::Game.ReferencedFiles.contains(file.ID) ? 1 : 0; });
                break;
            default: std::terminate();
        }
        #undef COMPARE
    }
    void SetResult(Utils::Async::Context context, std::vector<File>&& data)
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
            std::vector<File> data;
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

        if (FilterID && (FilterID->first >= 0x10000FF || FilterID->second >= 0x10000FF))
        {
            Data::Pack::FileReference ref;
            *(uint64*)&ref = FilterID->first;
            FilterID->first = ref.GetFileID();
            *(uint64*)&ref = FilterID->second;
            FilterID->second = ref.GetFileID();
        }

        AsyncFilter.Run([this, filter = FilterID, string = FilterString, type = FilterType, sort = Sort, invert = SortInvert](Utils::Async::Context context) mutable
        {
            context->SetIndeterminate();
            auto limits = filter.value_or(std::pair { std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() });
            limits.first = std::max(0, limits.first);
            std::vector data { std::from_range, G::Game.Archive.GetFiles() | std::views::filter([limits, type](File const& file)
            {
                if (file.ID < limits.first || file.ID > limits.second)
                    return false;

                if (!type)
                    return true;

                return G::ArchiveIndex[file.GetSourceKind()].GetFileMetadata(file.ID).Type == *type;
            }) };
            CHECK_ASYNC;
            SortList(context, data, sort, invert);
            SetResult(context, std::move(data));
        });
    }
    auto UpdateFilter() { UpdateSearch(); }

    void Draw() override
    {
        I::SetNextItemWidth(-(I::GetStyle().ItemSpacing.x + 60));
        if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &FilterString))
            UpdateSearch();
        Controls::AsyncProgressBar(AsyncFilter);
        I::SameLine();
        I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
        if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&FilterRange, 0.1f, 0, 10000))
            UpdateSearch();
        if (I::IsItemHovered())
            I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        I::SetNextItemWidth(-FLT_MIN);
        std::vector<std::optional<User::ArchiveIndex::Type>> values(1, std::nullopt);
        values.append_range(magic_enum::enum_values<User::ArchiveIndex::Type>());
        if (Controls::FilteredComboBox("##Type", FilterType, values,
        {
            .MaxHeight = 500,
            .Formatter = [](auto const& type) -> std::string
            {
                if (!type)
                    return std::format("<c=#8>{} {}</c>", ICON_FA_FILTER, "Any Type");

                return std::format("<c=#8>{}</c> {}", ICON_FA_FILTER, magic_enum::enum_name(*type));
            },
            .Filter = [](auto const& type, auto const& filter, auto const& options)
            {
                if (!type)
                    return filter.Filters.empty();

                return filter.PassFilter(magic_enum::enum_name(*type).data());
            },
        }))
            UpdateFilter();

        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, I::GetStyle().ItemSpacing / 2))
        if (scoped::Table("Table", 18, ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable))
        {
            I::TableSetupColumn("File ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth, 60, (ImGuiID)FileSort::ID);
            I::TableSetupColumn("##Stream", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoSort, I::GetTextLineHeight());
            I::TableSetupColumn("Archive", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth, 60, (ImGuiID)FileSort::Archive);
            I::TableSetupColumn("FourCC", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 30, (ImGuiID)FileSort::FourCC);
            I::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHeaderWidth, 2, (ImGuiID)FileSort::Type);
            I::TableSetupColumn("Metadata", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHeaderWidth, 3, (ImGuiID)FileSort::Metadata);
            I::TableSetupColumn("Manifest", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHeaderWidth, 3, (ImGuiID)FileSort::Manifest);
            I::TableSetupColumn("Added", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_PreferSortDescending, 50, (ImGuiID)FileSort::Added);
            I::TableSetupColumn("##Version", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoSort, I::GetTextLineHeight());
            I::TableSetupColumn("Changed", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_PreferSortDescending, 50, (ImGuiID)FileSort::Changed);
            I::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 70, (ImGuiID)FileSort::Size);
            I::TableSetupColumn("Compressed Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 70, (ImGuiID)FileSort::CompressedSize);
            I::TableSetupColumn("Extra Bytes", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 20, (ImGuiID)FileSort::ExtraBytes);
            I::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 20, (ImGuiID)FileSort::Flags);
            I::TableSetupColumn("Stream", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 20, (ImGuiID)FileSort::Stream);
            I::TableSetupColumn("Next Stream", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 30, (ImGuiID)FileSort::NextStream);
            I::TableSetupColumn("CRC", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 70, (ImGuiID)FileSort::CRC);
            I::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_PreferSortDescending, 40, (ImGuiID)FileSort::Refs);
            I::TableSetupScrollFreeze(0, 1);
            if (scoped::WithStyleVar(ImGuiStyleVar_FramePadding, ImVec2()))
                I::TableUpdateLayout(I::GetCurrentTable());
            I::TableHeadersRow();

            if (auto specs = I::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0)
            {
                Sort = (FileSort)specs->Specs[0].ColumnUserID;
                SortInvert = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                specs->SpecsDirty = false;
                UpdateSearch();
            }

            std::shared_lock __(Lock);
            ImGuiListClipper clipper;
            clipper.Begin(FilteredList.size());
            auto scrollTo = std::exchange(ScrollTo, std::nullopt);
            if (scrollTo)
                if (auto const itr = std::ranges::find(FilteredList, *scrollTo); itr != FilteredList.end())
                    clipper.IncludeItemByIndex(std::distance(FilteredList.begin(), itr));
            while (clipper.Step())
            {
                for (File const& file : std::span(FilteredList.begin() + clipper.DisplayStart, FilteredList.begin() + clipper.DisplayEnd))
                {
                    scoped::WithID(I::GetIDWithSeed(file.ID, file.GetSourceLoadOrder()));

                    auto const& entry = file.GetMftEntry();
                    auto const& index = G::ArchiveIndex[file.GetSourceKind()];
                    auto const& cache = index.GetFile(file.ID);
                    auto const& metadata = index.GetMetadata(cache.MetadataIndex);
                    auto const& addedTimestamp = index.GetTimestamp(cache.AddedTimestampIndex);
                    auto const& changedTimestamp = index.GetTimestamp(cache.ChangedTimestampIndex);
                    auto const& asset = file.GetManifestAsset();

                    I::TableNextRow();
                    I::TableNextColumn();
                    I::SetNextItemAllowOverlap();
                    I::Selectable(std::format("<c=#{}>{}</c>", asset.BaseID && asset.FileID && asset.BaseID != asset.FileID && file.ID == asset.FileID ? "4" : asset.StreamBaseID ? "8" : "F", file.ID).c_str(), FileViewer::Is(G::UI.GetCurrentViewer(), file), ImGuiSelectableFlags_SpanAllColumns);
                    if (scrollTo == file)
                        I::ScrollToItem();
                    if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                        FileViewer::Open(file, { .MouseButton = button });
                    if (scoped::PopupContextItem())
                    {
                        uint64 const fileRef = (0x100 + file.ID / 0xFF00) << 16 | 0xFF + file.ID % 0xFF00;
                        Controls::CopyButton("ID", file.ID); I::SameLine();
                        Controls::CopyButton("FileReference", fileRef); I::SameLine();
                        Controls::CopyButton("FileReference", std::format("{:s}", std::span { (byte const*)&fileRef, 8 } | std::views::transform([](byte b) { return std::format("{:02X}", b); }) | std::views::join_with(" "sv)));

                        Controls::CopyButton("Archive Name", file.GetSourcePath().filename().wstring()); I::SameLine();
                        Controls::CopyButton("Archive Path", file.GetSourcePath().wstring());

                        Controls::CopyButton("Type", magic_enum::enum_name(metadata.Type)); I::SameLine();
                        Controls::CopyButton("Metadata", metadata.DataToString()); I::SameLine();
                        Controls::CopyButton("Manifest", std::wstring { std::from_range, asset.ManifestNames | std::views::transform([](wchar_t const* manifestName) -> std::wstring_view { return manifestName; }) | std::views::join_with(L", "sv) }); I::SameLine();
                        Controls::CopyButton("FourCC", metadata.FourCCToString());

                        Controls::CopyButton("Added Build", addedTimestamp.Build); I::SameLine();
                        Controls::CopyButton("Added Timestamp", Utils::Format::DateTimeFullLocal(Time::FromTimestamp(addedTimestamp.Timestamp)));

                        Controls::CopyButton("Changed Build", changedTimestamp.Build); I::SameLine();
                        Controls::CopyButton("Changed Timestamp", Utils::Format::DateTimeFullLocal(Time::FromTimestamp(changedTimestamp.Timestamp)));

                        Controls::CopyButton("Size", cache.FileSize); I::SameLine();
                        Controls::CopyButton("Compressed Size", entry.alloc.size); I::SameLine();
                        Controls::CopyButton("Extra Bytes", entry.alloc.extraBytes); I::SameLine();
                        Controls::CopyButton("Offset", entry.alloc.offset);

                        Controls::CopyButton("Flags", entry.alloc.flags); I::SameLine();
                        Controls::CopyButton("Stream", entry.alloc.stream); I::SameLine();
                        Controls::CopyButton("Next Stream", entry.alloc.nextStream); I::SameLine();
                        Controls::CopyButton("CRC", std::format("{:08X}", entry.alloc.crc));

                        I::Dummy({ 1, 10 });

                        if (auto const version = G::Game.Archive.GetFileEntry(asset.BaseID); scoped::Disabled(!(version && asset.BaseID && asset.FileID && asset.BaseID != asset.FileID && file.ID == asset.FileID)))
                            if (Controls::FileButton(asset.BaseID, version, { .Icon = ICON_FA_CHEVRONS_LEFT, .Text = "Base Version", .TooltipPreview = false }))
                                ScrollTo = *version;
                        I::SameLine();
                        if (auto const version = G::Game.Archive.GetFileEntry(asset.FileID); scoped::Disabled(!(version && asset.BaseID && asset.FileID && asset.BaseID != asset.FileID && file.ID == asset.BaseID)))
                            if (Controls::FileButton(asset.FileID, version, { .Icon = ICON_FA_CHEVRONS_RIGHT, .Text = "Latest Version", .TooltipPreview = false }))
                                ScrollTo = *version;

                        if (auto const version = G::Game.Archive.GetFileEntry(asset.ParentBaseID); scoped::Disabled(!version))
                            if (Controls::FileButton(asset.ParentBaseID, version, { .Icon = ICON_FA_ARROW_DOWN_BIG_SMALL, .Text = "Lower Quality", .TextMissingFile = "Lower Quality", .TooltipPreviewBestVersion = false }))
                                ScrollTo = *version;
                        I::SameLine();
                        if (auto const version = G::Game.Archive.GetFileEntry(asset.StreamBaseID); scoped::Disabled(!version))
                            if (Controls::FileButton(asset.StreamBaseID, version, { .Icon = ICON_FA_ARROW_UP_BIG_SMALL, .Text = "Higher Quality", .TextMissingFile = "Higher Quality", .TooltipPreviewBestVersion = false }))
                                ScrollTo = *version;
                        if (ScrollTo)
                            I::CloseCurrentPopup();

                        I::Dummy({ 1, 10 });

                        if (I::Button("Search for Content References"))
                            G::Windows::ContentSearch.SearchForSymbolValue("FileID", file.ID);
                    }
                    I::TableNextColumn();
                    assert(!(asset.ParentBaseID && asset.StreamBaseID));
                    if (auto const parent = file.GetSource().GetFile(asset.ParentBaseID))
                    {
                        I::Selectable("<c=#4>" ICON_FA_ARROW_DOWN_BIG_SMALL "</c>");
                        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                            FileViewer::Open(ScrollTo.emplace(*parent), { .MouseButton = button });
                        if (scoped::ItemTooltip())
                            I::Text("Lower quality: %u", parent->ID);
                    }
                    else if (auto const stream = file.GetSource().GetFile(asset.StreamBaseID))
                    {
                        I::Selectable(ICON_FA_ARROW_UP_BIG_SMALL);
                        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                            FileViewer::Open(ScrollTo.emplace(*stream), { .MouseButton = button });
                        if (scoped::ItemTooltip())
                            I::Text("Higher quality: %u", stream->ID);
                    }
                    I::TableNextColumn(); I::Text("<c=#4>%s</c>", file.GetSourcePath().filename().string().c_str());
                    I::TableNextColumn(); I::Text("<c=#8>%s</c>", metadata.FourCCToString().c_str());
                    I::TableNextColumn(); I::Text(std::format("{}", magic_enum::enum_name(metadata.Type)).c_str());
                    I::TableNextColumn(); I::TextUnformatted(metadata.DataToString().c_str());
                    I::TableNextColumn();
                    if (!asset.ManifestNames.empty())
                    {
                        I::Text(asset.ManifestNames.size() == 1 ? "%s" : "%s +%u", Utils::Encoding::ToUTF8(*asset.ManifestNames.begin()).c_str(), (uint32)(asset.ManifestNames.size() - 1));
                        if (scoped::ItemTooltip(asset.ManifestNames.size() > 1 ? ImGuiHoveredFlags_DelayNone : ImGuiHoveredFlags_None))
                        {
                            I::TextUnformatted(asset.ManifestNames.size() > 1 ? "<c=#4>Included in manifests:</c>" : "<c=#4>Included in manifest:</c>");
                            for (auto const& manifestName : asset.ManifestNames)
                                I::TextUnformatted(Utils::Encoding::ToUTF8(manifestName).c_str());
                        }
                    }
                    auto timestamp = [](User::ArchiveIndex::CacheTimestamp const& timestamp, std::string_view description)
                    {
                        I::Text("<c=#%s>%u</c>", G::Game.Build ? (timestamp.Build >= G::Game.Build ? "F00" : timestamp.Build + 100 >= G::Game.Build ? "F80" : timestamp.Build + 1000 >= G::Game.Build ? "FF0": timestamp.Build + 5000 >= G::Game.Build ? "FF8" : "F") : "F", timestamp.Build);
                        if (scoped::ItemTooltip())
                            I::Text(std::format("<c=#4>{}:</c>\nBuild: {}\nDate: {}", description, timestamp.Build, Utils::Format::DateTimeFullLocal(Time::FromTimestamp(timestamp.Timestamp))).c_str());
                    };
                    I::TableNextColumn(); timestamp(addedTimestamp, "File first scanned");
                    I::TableNextColumn();
                    if (asset.BaseID && asset.FileID && asset.BaseID != asset.FileID)
                    {
                        if (file.ID == asset.FileID)
                        {
                            if (auto const base = file.GetSource().GetFile(asset.BaseID))
                            {
                                I::Selectable(ICON_FA_CHEVRONS_LEFT);
                                if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                                    FileViewer::Open(ScrollTo.emplace(*base), { .MouseButton = button });
                                if (scoped::ItemTooltip())
                                    I::Text("Base version: %u", base->ID);
                            }
                        }
                        else if (file.ID == asset.BaseID)
                        {
                            if (auto const latest = file.GetSource().GetFile(asset.FileID))
                            {
                                I::Selectable("<c=#4>" ICON_FA_CHEVRONS_RIGHT "</c>");
                                if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                                    FileViewer::Open(ScrollTo.emplace(*latest), { .MouseButton = button });
                                if (scoped::ItemTooltip())
                                    I::Text("Latest version: %u", latest->ID);
                            }
                        }
                    }
                    I::TableNextColumn(); timestamp(changedTimestamp, "Last time file change was scanned");
                    I::TableNextColumn(); I::Text("%u", cache.FileSize);
                    I::TableNextColumn(); I::Text("%u", entry.alloc.size);
                    I::TableNextColumn(); I::Text(entry.alloc.extraBytes ? "%u" : "<c=#4>%u</c>", (uint32)entry.alloc.extraBytes);
                    I::TableNextColumn(); I::Text(entry.alloc.flags ? "%u" : "<c=#4>%u</c>", entry.alloc.flags);
                    I::TableNextColumn(); I::Text(entry.alloc.stream ? "%u" : "<c=#4>%u</c>", entry.alloc.stream);
                    I::TableNextColumn(); I::Text(entry.alloc.nextStream ? "%u" : "<c=#4>%u</c>", entry.alloc.nextStream);
                    I::TableNextColumn(); I::Text("%08X", entry.alloc.crc);
                    I::TableNextColumn(); if (G::Game.ReferencedFiles.contains(file.ID)) I::TextColored({ 0, 0.5f, 1, 1 }, ICON_FA_ARROW_LEFT "EXE");
                }
            }
        }
    }
};

}
