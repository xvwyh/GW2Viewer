module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Windows.Settings;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.User.Config;
import std;

export namespace UI::Windows
{

struct Settings : Window
{
    bool Accepted = false;

    std::string Title() override { return "Settings"; }
    void Draw() override
    {
        I::InputText("Gw2-64.exe Path", &G::Config.GameExePath);
        I::InputText("Gw2.dat Path", &G::Config.GameDatPath);
        I::InputText("Local.dat Path (optional)", &G::Config.LocalDatPath);
        I::InputText("Decryption Keys DB (.sqlite/.txt) (optional)", &G::Config.DecryptionKeysPath);
        if (scoped::Disabled(G::Config.GameExePath.empty() || G::Config.GameDatPath.empty()))
        {
            if (I::Button("OK"))
            {
                Accepted = true;
                Hide();
            }
        }
    }
};

}

export namespace G::Windows { UI::Windows::Settings Settings; }
