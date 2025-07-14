module GW2Viewer.UI.Controls;
import GW2Viewer.UI.Viewers.ContentViewer;

void OpenContent(GW2Viewer::Data::Content::ContentObject& content, GW2Viewer::UI::Viewers::OpenViewerOptions const& options)
{
    GW2Viewer::UI::Viewers::ContentViewer::Open(content, options);
}
