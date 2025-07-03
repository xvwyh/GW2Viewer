export module GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.Common;
import GW2Viewer.Data.Archive;
import GW2Viewer.UI.Viewers.Viewer;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct FileViewer : Viewer
{
    static bool Is(Viewer const* viewer, Data::Archive::File const& file)
    {
        auto const* currentViewer = dynamic_cast<FileViewer const*>(viewer);
        return currentViewer && currentViewer->File == file;
    }

    Data::Archive::File File;
    std::stack<Data::Archive::File> HistoryPrev;
    std::stack<Data::Archive::File> HistoryNext;
    std::vector<byte> RawData;

    FileViewer(uint32 id, bool newTab, Data::Archive::File const& file) : Viewer(id, newTab), File(file), RawData(File.Source.get().Archive.GetFile(file.ID)) { }

    virtual void Initialize() { }

    virtual std::string Title();
    virtual void Draw();
    virtual void DrawOutline() { }
    virtual void DrawPreview();
};

}
