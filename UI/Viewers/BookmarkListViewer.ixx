module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Viewers.BookmarkListViewer;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Format;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct BookmarkListViewer : ListViewer<BookmarkListViewer, { ICON_FA_BOOKMARK " Bookmarks", "Bookmarks", Category::ListViewer }>
{
    using ListViewer::ListViewer;

    void Draw() override
    {
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, { I::GetStyle().FramePadding.x, 0 }))
        if (scoped::Table("Table", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable))
        {
            I::TableSetupColumn("Bookmark", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupScrollFreeze(0, 1);
            I::TableHeadersRow();

            for (auto const& bookmark : G::Config.BookmarkedContentObjects)
            {
                I::TableNextRow();
                I::TableNextColumn(); Controls::ContentButton(G::Game.Content.GetByGUID(bookmark.Value), &bookmark, { .MissingContentName = "CONTENT OBJECT MISSING" });
                I::TableNextColumn(); I::TextUnformatted(Utils::Format::DurationShortColored("{} ago", Time::UntilNowSecs(bookmark.Time)).c_str());
            }
        }
    }
};

}
