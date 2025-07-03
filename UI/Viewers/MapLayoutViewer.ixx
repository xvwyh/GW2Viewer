module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Viewers.MapLayoutViewer;
import GW2Viewer.Common.GUID;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Viewers.Viewer;

export namespace GW2Viewer::UI::Viewers
{

struct MapLayoutViewer : Viewer
{
    Controls::MapLayout MapLayout;

    MapLayoutViewer(uint32 id, bool newTab) : Viewer(id, newTab)
    {
        MapLayout.MapLayoutContinent = G::Game.Content.GetByGUID({ "21742531-35A3-4763-A670-8B774B2B27AC" });
        MapLayout.MapLayoutContinentFloor = G::Game.Content.GetByGUID({ "DD8189AD-0359-4C17-AD7B-5CA5B33E52F3" });
        MapLayout.Initialize();
    }

    std::string Title() override { return ICON_FA_GLOBE " World Map"; }
    void Draw() override
    {
        MapLayout.Draw();
    }
};

}
