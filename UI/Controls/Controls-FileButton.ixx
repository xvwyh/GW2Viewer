module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Controls:FileButton;
import :Texture;
import GW2Viewer.Data.Archive;
import GW2Viewer.UI.Viewers.Viewer;
import std;

namespace GW2Viewer::UI::Controls
{
void OpenFile(Data::Archive::File const& file, Viewers::OpenViewerOptions const& options);

export
{

struct FileButtonOptions
{
    std::string_view Icon = ICON_FA_FILE;
    std::string_view Text = "File";
    std::string_view TextMissingFile = "<c=#F00>Missing File</c>";
    bool OpenViewer = true;
    bool TooltipPreview = true;
    bool TooltipPreviewBestVersion = true;
};
bool FileButton(uint32 fileID, Data::Archive::File const* file, FileButtonOptions const& options = { })
{
    bool result = I::Button(std::vformat(options.Text.empty() ? "<c=#{3}>{0} {2}</c>" : "<c=#{3}>{0} {1}: {2}</c>", std::make_format_args(options.Icon, file || !fileID ? options.Text : options.TextMissingFile, fileID, fileID ? "F" : "4")).c_str());
    if (options.OpenViewer && file)
    {
        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
        {
            OpenFile(*file, { .MouseButton = button });
            result = true;
        }
    }

    if (options.TooltipPreview)
        if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
            Texture(fileID, { .BestVersion = options.TooltipPreviewBestVersion });

    return result;
}
bool FileButton(Data::Archive::File const& file, FileButtonOptions const& options = { })
{
    return FileButton(file.ID, &file, options);
}
bool FileButton(uint32 fileID, FileButtonOptions const& options = { })
{
    return FileButton(fileID, G::Game.Archive.GetFileEntry(fileID), options);
}

}

}
