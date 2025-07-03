module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Windows.Window;

export namespace GW2Viewer::UI::Windows { struct Window; }

export namespace GW2Viewer::G::Windows { auto& GetAllWindows() { static std::list<UI::Windows::Window*> instance; return instance; } }

export namespace GW2Viewer::UI::Windows
{

struct Window
{
    Window()
    {
        G::Windows::GetAllWindows().emplace_back(this);
    }
    virtual ~Window()
    {
        G::Windows::GetAllWindows().remove(this);
    }

    void Show() { m_shown = true; }
    void Hide() { m_shown = false; }
    auto& GetShown() { return m_shown; }

    void Update()
    {
        if (scoped::Window(Title().c_str(), &m_shown, ImGuiWindowFlags_NoFocusOnAppearing))
            Draw();
    }
    virtual std::string Title() = 0;
    virtual void Draw() = 0;

private:
    bool m_shown = false;
};

}
