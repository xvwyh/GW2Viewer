module GW2Viewer.UI.Controls;
import GW2Viewer.Data.Archive;
import GW2Viewer.UI.Viewers.ContentViewer;
import GW2Viewer.UI.Viewers.FileViewer;

namespace GW2Viewer::UI::Controls
{

void OpenContent(Data::Content::ContentObject& content, Viewers::OpenViewerOptions const& options)
{
    Viewers::ContentViewer::Open(content, options);
}

void OpenFile(Data::Archive::File const& file, Viewers::OpenViewerOptions const& options)
{
    Viewers::FileViewer::Open(file, options);
}

}
