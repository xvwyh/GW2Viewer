module;
#include "UI/ImGui/ImGui.h"

module GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.FileViewers;
import GW2Viewer.Utils.Exception;

namespace GW2Viewer::UI::Viewers
{

void FileViewer::Open(TargetType target, OpenViewerOptions const& options)
{
    if (I::GetIO().KeyAlt)
    {
        auto data = target.Source.get().Archive.GetFile(target.ID);
        G::UI.ExportData(data, std::format(R"(Export\{})", target.ID));
        G::Game.Texture.Load(target.ID, { .DataSource = &data, .Export = true });
        return;
    }

    Base::Open(target, options);
}

std::string FileViewer::Title()
{
    if (&File.Source.get().Archive != G::Game.Archive.GetArchive())
        return std::format("<c=#4>{} #</c>{}<c=#4> ({})</c>", Base::Title(), File.ID, File.Source.get().Path.filename().string());
    return std::format("<c=#4>{} #</c>{}", Base::Title(), File.ID);
}

void FileViewer::Draw()
{
    auto _ = Utils::Exception::SEHandler::Create();

    bool drawHex = false;
    bool drawOutline = false;
    bool drawPreview = false;
    if (scoped::Child(I::GetSharedScopeID("FileViewer"), { }, ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
    {
        DrawHistoryButtons();

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

void FileViewer::DrawPreview()
{
    Controls::Texture(File.ID, { .Data = &RawData });
}

std::unique_ptr<FileViewer> Init(uint32 id, bool newTab, FileViewer::TargetType file)
{
    std::unique_ptr<FileViewer> result = nullptr;
    if (auto const data = file.Source.get().Archive.GetFile(file.ID); data.size() >= 4) // TODO: Refactor to avoid copying
    {
        auto&& registry = FileViewers::GetRegistry();
        if (auto const itr = registry.find(*(fcc const*)data.data()); itr != registry.end())
            result = std::unique_ptr<FileViewer>(itr->second(id, newTab, file));
    }
    if (!result)
        result = std::make_unique<FileViewer>(id, newTab, file);
    result->Initialize();
    return result;
}

std::unique_ptr<FileViewer> FileViewer::Create(HistoryType target, OpenViewerOptions const& options)
{
    return Init(G::UI.GetNewViewerID(), options.OpenInNewTab, target);
}

void FileViewer::Recreate(ViewerType*& viewer, HistoryType target, OpenViewerOptions const& options)
{
    viewer = &G::UI.ReplaceViewer(*viewer, Init(viewer->ID, options.OpenInNewTab, target));
}

}
