module;
#include "UI/ImGui/ImGui.h"

module GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Manager;
import GW2Viewer.Utils.Exception;

std::string UI::Viewers::FileViewer::Title()
{
    if (&File.Source.get().Archive != G::Game.Archive.GetArchive())
        return std::format("<c=#4>File #</c>{}<c=#4> ({})</c>", File.ID, File.Source.get().Path.filename().string());
    return std::format("<c=#4>File #</c>{}", File.ID);
}

void UI::Viewers::FileViewer::Draw()
{
    auto _ = Utils::Exception::SEHandler::Create();

    bool drawHex = false;
    bool drawOutline = false;
    bool drawPreview = false;
    if (static ImGuiID sharedScope = 2; scoped::Child(sharedScope, { }, ImGuiChildFlags_Border | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
    {
        if (scoped::Disabled(HistoryPrev.empty()); I::Button(ICON_FA_ARROW_LEFT "##HistoryBack") || I::IsEnabled() && I::GetIO().MouseClicked[3])
        {
            auto const file = HistoryPrev.top();
            HistoryPrev.pop();
            HistoryNext.emplace(File);
            G::UI.OpenFile(file, false, true);
        }
        I::SameLine(0, 0);
        if (scoped::Disabled(HistoryNext.empty()); I::Button(ICON_FA_ARROW_RIGHT "##HistoryNext") || I::IsEnabled() && I::GetIO().MouseClicked[4])
        {
            auto const file = HistoryNext.top();
            HistoryNext.pop();
            HistoryPrev.emplace(File);
            G::UI.OpenFile(file, false, true);
        }
        I::SameLine();
        if (scoped::TabBar("Tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_NoTabListScrollingButtons))
        {
            if (scoped::TabItem(ICON_FA_INFO " Info", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
            if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
            {
                drawHex = true;
            }
            if (scoped::TabItem(ICON_FA_BINARY " Data", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                drawHex = true;
            if (scoped::TabItem(ICON_FA_FOLDER_TREE " Outline", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                drawOutline = true;
            if (scoped::TabItem(ICON_FA_IMAGE " Preview", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                drawPreview = true;
        }
    }

    if (drawOutline)
        if (scoped::Child("Outline"))
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
            DrawOutline();
    if (drawPreview)
        DrawPreview();

    if (!drawHex)
        return;

    static std::optional<uint32> highlightOffset;
    Controls::HexViewerOptions options
    {
        .ShowHeaderRow = true,
        .ShowOffsetColumn = true,
        .FillWindow = true,
        .OutHighlightOffset = highlightOffset,
    };
    HexViewer(RawData, options);
    if (auto const offset = options.OutHighlightOffset)
        highlightOffset = offset;
    else if (options.OutHoveredInfo)
        highlightOffset.reset();
    if (options.OutHoveredInfo)
    {
        auto const& [byteOffset, cellCursor, cellSize, tableCursor, tableSize] = *options.OutHoveredInfo;
        I::GetWindowDrawList()->AddRectFilled(ImVec2(tableCursor.x, cellCursor.y + 2), ImVec2(tableCursor.x + tableSize.x, cellCursor.y + cellSize.y - 2), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.2f }));
        I::GetWindowDrawList()->AddRectFilled(ImVec2(cellCursor.x + 2, tableCursor.y), ImVec2(cellCursor.x + cellSize.x - 2, tableCursor.y + tableSize.y), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.2f }));
    }
}

void UI::Viewers::FileViewer::DrawPreview()
{
    Controls::Texture(File.ID, { .Data = &RawData });
}

