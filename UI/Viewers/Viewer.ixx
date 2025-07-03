export module GW2Viewer.UI.Viewers.Viewer;
import GW2Viewer.Common;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct Viewer
{
    uint32 const ID;
    bool SetSelected = false;
    bool SetAfterCurrent = false;

    virtual ~Viewer() = default;

    virtual std::string Title() = 0;
    virtual void Draw() = 0;

protected:
    Viewer(uint32 id, bool newTab) : ID(id), SetSelected(!newTab), SetAfterCurrent(newTab) { }
};

}
