module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Viewers.FileListViewer;
import GW2Viewer.Common;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Pack;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Windows.ContentSearch;
import GW2Viewer.Utils.Scan;
import std;

export namespace UI::Viewers
{

struct FileListViewer : ListViewer<FileListViewer>
{
    FileListViewer(uint32 id, bool newTab) : ListViewer(id, newTab) { }

    std::vector<Data::Archive::File> FilteredList;
    std::span<Data::Archive::File> SearchedList;

    std::string FilterString;
    uint32 FilterID { };
    uint32 FilterRange { };

    void UpdateSearch()
    {
        if (FilterID >= 0x10000FF)
        {
            Data::Pack::FileReference ref;
            *(uint64*)&ref = FilterID;
            FilterID = ref.GetFileID();
        }
        if (FilterID)
        {
            auto const to = (uint32)std::max(0, (int32)FilterID + (int32)FilterRange);
            auto const from = (uint32)std::max(0, (int32)FilterID - (int32)FilterRange);
            SearchedList = { std::ranges::lower_bound(FilteredList, from, { }, &Data::Archive::File::ID), std::ranges::upper_bound(FilteredList, to, { }, &Data::Archive::File::ID) };
            return;
        }
        SearchedList = FilteredList;
    }
    void UpdateFilter()
    {
        auto filter = [this](Data::Archive::File const& file) { return true; }; // TODO
        FilteredList.assign_range(G::Game.Archive.GetFiles() | std::views::filter(filter));
        UpdateSearch();
    }

    std::string Title() override { return ICON_FA_FILE " Files"; }
    void Draw() override
    {
        I::SetNextItemWidth(-60);
        if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &FilterString, ImGuiInputTextFlags_CharsDecimal))
        {
            if (!Utils::Scan::Into(FilterString, FilterID))
                FilterID = 0;
            UpdateSearch();
        }
        I::SameLine();
        I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
        if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&FilterRange, 0.1f, 0, 10000))
            UpdateSearch();
        if (I::IsItemHovered())
            I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        if (I::SetNextItemWidth(-FLT_MIN); scoped::Combo("##Type", "Any Type"))
            UpdateFilter();

        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, I::GetStyle().ItemSpacing / 2))
        if (scoped::Table("Table", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX))
        {
            I::TableSetupColumn("File ID", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupColumn("Archive", ImGuiTableColumnFlags_WidthStretch);
            ImGuiListClipper clipper;
            clipper.Begin(SearchedList.size());
            while (clipper.Step())
            {
                for (Data::Archive::File const& file : std::span(SearchedList.begin() + clipper.DisplayStart, SearchedList.begin() + clipper.DisplayEnd))
                {
                    I::TableNextRow();
                    I::TableNextColumn(); I::Selectable(std::format("{}", file.ID).c_str(), FileViewer::Is(G::UI.GetCurrentViewer(), file), ImGuiSelectableFlags_SpanAllColumns);
                    if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                        G::UI.OpenFile(file, button & ImGuiButtonFlags_MouseButtonMiddle);
                    if (scoped::PopupContextItem())
                    {
                        if (I::Button("Search for Content References"))
                            G::Windows::ContentSearch.SearchForSymbolValue("FileID", file.ID);
                    }
                    I::TableNextColumn(); I::Text("<c=#4>%s</c>", file.Source.get().Path.filename().string().c_str());
                }
            }
        }
    }
};

}
