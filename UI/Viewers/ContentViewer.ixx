export module GW2Viewer.UI.Viewers.ContentViewer;
import GW2Viewer.Common;
import GW2Viewer.Data.Content;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.UI.Viewers.ViewerWithHistory;
import std;
#include "Macros.h"

export namespace GW2Viewer::UI::Viewers
{

struct ContentViewer : ViewerWithHistory<ContentViewer, Data::Content::ContentObject const&, { ICON_FA_CUBE " ContentObject", "ContentObject", Category::ObjectViewer }>
{
    TargetType Content;

    ContentViewer(uint32 id, bool newTab, Data::Content::ContentObject const& content) : Base(id, newTab), Content(content)
    {
        content.Finalize();
    }

    TargetType GetCurrent() const override { return Content; }
    bool IsCurrent(TargetType target) const override { return &Content == &target; }

    std::string Title() override;
    void Draw() override;

private:
    std::optional<Controls::HexViewerCellInfo> persistentHovered;
    std::optional<std::tuple<Data::Content::TypeInfo::LayoutStack, std::string, uint32, bool, bool>> creatingSymbol;
    std::optional<std::tuple<Data::Content::TypeInfo::LayoutStack, std::string, Data::Content::TypeInfo::Symbol*, ImVec2, ImVec2, bool>> editingSymbol;
    std::optional<uint32> highlightOffset;
    std::optional<byte const*> highlightPointer;
};

}
