export module GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.Common;
import GW2Viewer.Data.Archive;
import GW2Viewer.UI.Viewers.Viewer;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.UI.Viewers.ViewerWithHistory;
import std;
#include "Macros.h"

export namespace GW2Viewer::UI::Viewers
{

struct FileViewer : ViewerWithHistory<FileViewer, Data::Archive::File, { ICON_FA_FILE " File", "File", Category::ObjectViewer }>
{
    static bool Is(Viewer const* viewer, Data::Archive::File const& file)
    {
        auto const* currentViewer = dynamic_cast<FileViewer const*>(viewer);
        return currentViewer && currentViewer->File == file;
    }

    TargetType File;
    std::vector<byte> RawData;

    FileViewer(uint32 id, bool newTab, TargetType file) : Base(id, newTab), File(file), RawData(File.GetData()) { }

    TargetType GetCurrent() const override { return File; }
    bool IsCurrent(TargetType target) const override { return File == target; }
    static void Open(TargetType target, OpenViewerOptions const& options = { });

    virtual void Initialize() { }

    std::string Title() override;
    void Draw() override;
    virtual void DrawOutline() { }
    virtual void DrawPreview();

    static std::unique_ptr<ViewerType> Create(HistoryType target, OpenViewerOptions const& options);
    static void Recreate(ViewerType*& viewer, HistoryType target, OpenViewerOptions const& options);
};

}
