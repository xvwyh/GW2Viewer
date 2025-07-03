export module GW2Viewer.UI.Viewers.ContentViewer;
import GW2Viewer.Common;
import GW2Viewer.Data.Content;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.UI.Viewers.Viewer;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct ContentViewer : Viewer
{
    Data::Content::ContentObject& Content;
    std::stack<Data::Content::ContentObject*> HistoryPrev;
    std::stack<Data::Content::ContentObject*> HistoryNext;

    ContentViewer(uint32 id, bool newTab, Data::Content::ContentObject& content) : Viewer(id, newTab), Content(content)
    {
        content.Finalize();
    }

    std::string Title() override;
    void Draw() override;
};

}
