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

    std::string FilterString;
    std::optional<std::pair<int32, int32>> FilterID;
    uint32 FilterRange { };
    std::optional<User::ArchiveIndex::Type> FilterType;
    enum class FileSort { ID, Archive, FourCC, Type, Metadata, Added, Changed, Size, CompressedSize, ExtraBytes, Flags, Stream, NextStream, CRC, Refs } Sort { FileSort::ID };
    bool SortInvert { };

    void SortList(Utils::Async::Context context, std::vector<File>& data, FileSort sort, bool invert)
    {
        #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
        switch (sort)
        {
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
                ComplexSort(data, invert, [](File const& file) { return file.GetEntry().alloc.size; });
                break;
            case ExtraBytes:
                ComplexSort(data, invert, [](File const& file) { return file.GetEntry().alloc.extraBytes; });
                break;
            case Flags:
                ComplexSort(data, invert, [](File const& file) { return file.GetEntry().alloc.flags; });
                break;
            case Stream:
                ComplexSort(data, invert, [](File const& file) { return file.GetEntry().alloc.stream; });
                break;
            case NextStream:
                ComplexSort(data, invert, [](File const& file) { return file.GetEntry().alloc.nextStream; });
                break;
            case CRC:
                ComplexSort(data, invert, [](File const& file) { return file.GetEntry().alloc.crc; });
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
        if (scoped::Table("Table", 15, ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable))
        {
            I::TableSetupColumn("File ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth, 60, (ImGuiID)FileSort::ID);
            I::TableSetupColumn("Archive", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth, 60, (ImGuiID)FileSort::Archive);
            I::TableSetupColumn("FourCC", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 30, (ImGuiID)FileSort::FourCC);
            I::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHeaderWidth, 2, (ImGuiID)FileSort::Type);
            I::TableSetupColumn("Metadata", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHeaderWidth, 3, (ImGuiID)FileSort::Metadata);
            I::TableSetupColumn("Added", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth, 50, (ImGuiID)FileSort::Added);
            I::TableSetupColumn("Changed", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth, 50, (ImGuiID)FileSort::Changed);
            I::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 70, (ImGuiID)FileSort::Size);
            I::TableSetupColumn("Compressed Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 70, (ImGuiID)FileSort::CompressedSize);
            I::TableSetupColumn("Extra Bytes", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 20, (ImGuiID)FileSort::ExtraBytes);
            I::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 20, (ImGuiID)FileSort::Flags);
            I::TableSetupColumn("Stream", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 20, (ImGuiID)FileSort::Stream);
            I::TableSetupColumn("Next Stream", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 30, (ImGuiID)FileSort::NextStream);
            I::TableSetupColumn("CRC", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_DefaultHide, 70, (ImGuiID)FileSort::CRC);
            I::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth | ImGuiTableColumnFlags_PreferSortDescending, 40, (ImGuiID)FileSort::Refs);
            I::TableSetupScrollFreeze(0, 1);
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
            while (clipper.Step())
            {
                for (File const& file : std::span(FilteredList.begin() + clipper.DisplayStart, FilteredList.begin() + clipper.DisplayEnd))
                {
                    scoped::WithID(I::GetIDWithSeed(file.ID, file.GetSourceLoadOrder()));

                    auto entry = file.GetEntry();
                    auto const& index = G::ArchiveIndex[file.GetSourceKind()];
                    auto const& cache = index.GetFile(file.ID);
                    auto const& metadata = index.GetMetadata(cache.MetadataIndex);
                    auto const& addedTimestamp = index.GetTimestamp(cache.AddedTimestampIndex);
                    auto const& changedTimestamp = index.GetTimestamp(cache.ChangedTimestampIndex);

                    I::TableNextRow();
                    I::TableNextColumn();
                    I::SetNextItemAllowOverlap();
                    I::Selectable(std::format("{}", file.ID).c_str(), FileViewer::Is(G::UI.GetCurrentViewer(), file), ImGuiSelectableFlags_SpanAllColumns);
                    if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                        FileViewer::Open(file, { .MouseButton = button });
                    if (scoped::PopupContextItem())
                    {
                        Controls::CopyButton("ID", file.ID); I::SameLine();
                        Controls::CopyButton("FileReference", (0x100 + file.ID / 0xFF00) << 16 | 0xFF + file.ID % 0xFF00);

                        Controls::CopyButton("Archive Name", file.GetSourcePath().filename().wstring()); I::SameLine();
                        Controls::CopyButton("Archive Path", file.GetSourcePath().wstring());

                        Controls::CopyButton("Type", magic_enum::enum_name(metadata.Type)); I::SameLine();
                        Controls::CopyButton("Metadata", metadata.DataToString()); I::SameLine();
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

                        if (I::Button("Search for Content References"))
                            G::Windows::ContentSearch.SearchForSymbolValue("FileID", file.ID);
                    }
                    I::TableNextColumn(); I::Text("<c=#4>%s</c>", file.GetSourcePath().filename().string().c_str());
                    I::TableNextColumn(); I::Text("<c=#8>%s</c>", metadata.FourCCToString().c_str());
                    I::TableNextColumn(); I::Text(std::format("{}", magic_enum::enum_name(metadata.Type)).c_str());
                    I::TableNextColumn(); I::TextUnformatted(metadata.DataToString().c_str());
                    auto timestamp = [](char const* id, User::ArchiveIndex::CacheTimestamp const& timestamp, std::string_view description)
                    {
                        I::Selectable(std::format("<c=#{}>{}</c>##{}", G::Game.Build ? (timestamp.Build >= G::Game.Build ? "F00" : timestamp.Build + 100 >= G::Game.Build ? "F80" : timestamp.Build + 1000 >= G::Game.Build ? "FF0": timestamp.Build + 5000 >= G::Game.Build ? "FF8" : "F") : "F", timestamp.Build, id).c_str());
                        if (scoped::ItemTooltip())
                            I::Text(std::format("{}:\nBuild: {}\nDate: {}", description, timestamp.Build, Utils::Format::DateTimeFullLocal(Time::FromTimestamp(timestamp.Timestamp))).c_str());
                    };
                    I::TableNextColumn(); timestamp("AddedTimestamp", addedTimestamp, "File first scanned");
                    I::TableNextColumn(); timestamp("ChangedTimestamp", changedTimestamp, "Last time file changed was scanned");
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
