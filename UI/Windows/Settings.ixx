export module GW2Viewer.UI.Windows.Settings;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.User.Config;
import std;
#include "Macros.h"

export namespace GW2Viewer::UI::Windows
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
        I::InputText("Decryption Keys DB (.sqlite) (optional)", &G::Config.DecryptionKeysPath);
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

export namespace GW2Viewer::G::Windows { UI::Windows::Settings Settings; }
