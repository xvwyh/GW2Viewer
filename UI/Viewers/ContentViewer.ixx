module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Viewers.ContentViewer;
import GW2Viewer.Common;
import GW2Viewer.Data.Content;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.UI.Viewers.ViewerWithHistory;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct ContentViewer : ViewerWithHistory<ContentViewer, Data::Content::ContentObject&, { ICON_FA_CUBE " ContentObject", "ContentObject", Category::ObjectViewer }>
{
    TargetType Content;

    ContentViewer(uint32 id, bool newTab, Data::Content::ContentObject& content) : Base(id, newTab), Content(content)
    {
        content.Finalize();
    }

    TargetType GetCurrent() const override { return Content; }
    bool IsCurrent(TargetType target) const override { return &Content == &target; }

    std::string Title() override;
    void Draw() override;
};

}
