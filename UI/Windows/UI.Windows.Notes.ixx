module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Windows.Notes;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.User.Config;
import std;

export namespace UI::Windows
{

struct Notes : Window
{
    std::string Title() override { return "Notes"; }
    void Draw() override
    {
        I::SetNextItemWidth(-FLT_MIN);
        I::InputTextMultiline("##Notes", &G::Config.Notes, { -1, -1 }, ImGuiInputTextFlags_AllowTabInput);
    }
};

}

export namespace G::Windows { UI::Windows::Notes Notes; }
