export module GW2Viewer.UI.Viewers.Viewer;
import GW2Viewer.Common;
import GW2Viewer.UI.ImGui;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct OpenViewerOptions
{
    ImGuiButtonFlags_ MouseButton = ImGuiButtonFlags_MouseButtonLeft;
    bool OpenInNewTab = (bool)(MouseButton & ImGuiButtonFlags_MouseButtonMiddle);
    bool HistoryMove = false;
};

struct Viewer
{
    uint32 const ID;
    ImGuiWindow* ImGuiWindow = nullptr;
    bool SetSelected = false;
    bool SetAfterCurrent = false;

    virtual ~Viewer() = default;

    virtual std::string Title() = 0;
    virtual void Draw() = 0;

protected:
    Viewer(uint32 id, bool newTab) : ID(id), SetSelected(!newTab), SetAfterCurrent(newTab) { }
};

}
