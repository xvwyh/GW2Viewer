export module GW2Viewer.UI.Windows.MigrateContentTypes;
import GW2Viewer.Common;
import GW2Viewer.Content;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.User.Config;
import std;
import magic_enum;
#include "Macros.h"

export namespace GW2Viewer::UI::Windows
{

struct MigrateContentTypes : Window
{
    std::string Title() override { return "Migrate Content Types"; }
    void Draw() override
    {
        static auto mappings = []
        {
            std::vector<uint32> mappings(G::Config.LastNumContentTypes);
            std::ranges::iota(mappings, 0);
            uint32 offset = 0;
            for (auto&& [oldIndex, newIndex] : mappings | std::views::enumerate)
            {
                if (auto const& examples = G::Config.TypeInfo[oldIndex].Examples; !examples.empty())
                {
                    for (auto const& example : examples)
                    {
                        if (auto const object = G::Game.Content.GetByGUID(example))
                        {
                            offset = object->Type->Index - oldIndex;
                            break;
                        }
                    }
                }
                newIndex += offset;
            }
            return mappings;
        }();

        if (scoped::Disabled(G::Config.LastNumContentTypes == G::Game.Content.GetNumTypes()))
        if (I::Button("Migrate"))
        {
            G::Config.LastNumContentTypes = G::Game.Content.GetNumTypes();
            decltype(G::Config.TypeInfo) newTypeInfo;
            for (auto&& [oldIndex, newIndex] : mappings | std::views::enumerate | std::views::reverse)
            {
                if (auto const itr = G::Config.TypeInfo.find(oldIndex); itr != G::Config.TypeInfo.end())
                {
                    auto node = G::Config.TypeInfo.extract(itr);
                    node.key() = newIndex;
                    newTypeInfo.insert(std::move(node));
                }
            }
            G::Config.TypeInfo = std::move(newTypeInfo);
        }
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("Differences", 5))
        {
            I::TableSetupColumn("OldIndex", ImGuiTableColumnFlags_WidthFixed, 30);
            I::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("NewIndex", ImGuiTableColumnFlags_WidthFixed, 30);
            I::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupColumn("ContentType", ImGuiTableColumnFlags_WidthStretch);

            for (auto&& [oldIndex, newIndex] : mappings | std::views::enumerate)
            {
                scoped::WithID(oldIndex);
                bool const changed = oldIndex != newIndex;

                I::TableNextRow();

                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                if (auto index = std::format("{}", oldIndex); scoped::WithColorVar(ImGuiCol_Text, changed ? 0xFF0000FF : I::GetColorU32(ImGuiCol_Text)))
                    I::InputText("##OldIndex", &index, ImGuiInputTextFlags_ReadOnly);

                I::TableNextColumn();
                I::TextUnformatted(ICON_FA_ARROW_RIGHT);

                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                if (auto index = std::format("{}", newIndex); scoped::WithColorVar(ImGuiCol_Text, changed ? 0xFF00FF00 : I::GetColorU32(ImGuiCol_Text)))
                    I::InputText("##NewIndex", &index, ImGuiInputTextFlags_ReadOnly);

                if (auto const itr = G::Config.TypeInfo.find(oldIndex); itr != G::Config.TypeInfo.end())
                {
                    I::TableNextColumn();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::InputText("##Name", &itr->second.Name);

                    I::TableNextColumn();
                    I::SetNextItemWidth(-FLT_MIN);
                    Controls::FilteredComboBox("##ContentType", itr->second.ContentType, magic_enum::enum_values<Content::EContentTypes>());
                }
            }
        }
    }
};

}

export namespace GW2Viewer::G::Windows { UI::Windows::MigrateContentTypes MigrateContentTypes; }
