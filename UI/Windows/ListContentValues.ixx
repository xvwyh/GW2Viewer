export module GW2Viewer.UI.Windows.ListContentValues;
import GW2Viewer.Common;
import GW2Viewer.Data.Content;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.Utils.Sort;
import std;
#include "Macros.h"

export namespace GW2Viewer::UI::Windows
{

struct ListContentValues : Window
{
    struct CachedKey
    {
        std::span<byte const> Data;
        Data::Content::TypeInfo::SymbolType const* Type = nullptr;

        auto operator<=>(CachedKey const& other) const
        {
            return Type && Type == other.Type
                ? Type->CompareDataForSearch(Data.data(), other.Data.data())
                : std::lexicographical_compare_three_way(Data.rbegin(), Data.rend(), other.Data.rbegin(), other.Data.rend());
        }
    };
    struct CachedValue
    {
        Data::Content::TypeInfo::Symbol Symbol;
        byte const* Data;
        std::unordered_set<Data::Content::ContentObject const*> Objects;
        std::vector<Data::Content::ContentObject const*> ObjectsSorted;
        bool IsFolded = true;
    };

    Data::Content::ContentTypeInfo const* Type = nullptr;
    std::string SymbolPath;
    bool IsEnum = false;
    bool IncludeZero = false;
    bool AsFlags = false;
    std::map<CachedKey, CachedValue> Results;
    std::set<Data::Content::TypeInfo::Condition::ValueType> ExternalKeyStorage;

    void Set(Data::Content::ContentTypeInfo const& type, Data::Content::TypeInfo::Symbol const& symbol, Data::Content::TypeInfo::LayoutStack const& layoutStack)
    {
        Type = &type;
        SymbolPath = symbol.GetFullPath(*layoutStack.top().Path);
        IsEnum = symbol.GetEnum();
        IncludeZero = IsEnum && !symbol.GetEnum()->Flags;
        AsFlags = IsEnum && symbol.GetEnum()->Flags;
        Refresh();
        Show();
    }
    void Refresh()
    {
        Results.clear();
        ExternalKeyStorage.clear();
        for (Data::Content::SymbolPath const path { SymbolPath }; auto const object : Type->Objects)
        {
            for (auto& result : Data::Content::QuerySymbolData(*object, path))
            {
                if (CachedKey key { { &result.Data<byte>(), result.Symbol.Size() }, result.Symbol.GetType() }; IncludeZero || std::ranges::any_of(key.Data, std::identity())) // Only show non-zero values
                {
                    if (auto const e = result.Symbol.GetEnum(); e && e->Flags && AsFlags)
                    {
                        if (auto value = key.Type->GetValueForCondition({ &result.Data<byte>(), *object, result.Symbol }).value_or(0))
                        {
                            for (decltype(value) flag = 1; flag; flag <<= 1)
                            {
                                if (value & flag)
                                {
                                    auto data = (byte const*)&*ExternalKeyStorage.emplace(flag).first;
                                    Results.try_emplace({ { data, sizeof(decltype(ExternalKeyStorage)::value_type) } }, result.Symbol, data).first->second.Objects.emplace(object);
                                }
                            }
                            continue;
                        }
                    }
                    Results.try_emplace(key, result.Symbol, &result.Data<byte>()).first->second.Objects.emplace(object);
                }
            }
        }
        for (auto& value : Results | std::views::values)
            value.ObjectsSorted = Utils::Sort::ComplexSorted(value.Objects, false, [](Data::Content::ContentObject const* object) { return std::make_tuple(object->GetFullDisplayName(), object->GetFullName(), object->Type->Index, object->Index); });
    }

    std::string Title() override { return "List Content Values"; }
    void Draw() override
    {
        if (I::Button(ICON_FA_ARROWS_ROTATE " Refresh"))
            Refresh();
        if (I::SameLine(), I::Checkbox("Include Zero", &IncludeZero))
            Refresh();
        if (IsEnum && (I::SameLine(), I::Checkbox("As Flags", &AsFlags)))
            Refresh();

        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
        if (scoped::Table("UniqueFieldValues", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings))
        {
            I::TableSetupColumn(SymbolPath.c_str(), 0, 1);
            I::TableSetupColumn("##Fold", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            I::TableSetupColumn("Content", 0, 3);
            I::TableSetupScrollFreeze(0, 1);
            I::TableHeadersRow();

            auto const tableContentsCursor = I::GetCursorScreenPos();

            ImGuiListClipper clipper;
            clipper.Begin(std::ranges::fold_left(Results, 0u, [](uint32 count, auto const& pair) { return count + (pair.second.IsFolded ? 1 : pair.second.Objects.size()); })/*, I::GetFrameHeight()*/);
            std::set<decltype(Results)::key_type> keyDrawn;
            while (clipper.Step())
            {
                int drawn = 0;
                int offset = 0;
                for (auto& [key, value] : Results)
                {
                    int numToDisplay = value.IsFolded ? 1 : value.ObjectsSorted.size();
                    auto displayedObjects = value.ObjectsSorted | std::views::take(numToDisplay) | std::views::drop(std::max(0, clipper.DisplayStart - offset)) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart - drawn);
                    bool const canAdjustY = !value.IsFolded && displayedObjects.size() > 1;
                    bool first = !keyDrawn.contains(key);
                    for (auto* object : displayedObjects)
                    {
                        I::TableNextRow();

                        float const yOffset = canAdjustY && I::GetCursorScreenPos().y < tableContentsCursor.y ? tableContentsCursor.y - I::GetCursorScreenPos().y : 0;

                        I::TableNextColumn();
                        if (first)
                        {
                            if (yOffset)
                                I::SetCursorPosY(I::GetCursorPosY() + yOffset);
                            value.Symbol.Draw(value.Data, Data::Content::TypeInfo::Symbol::DrawType::TableRow, *object);
                            I::GetCurrentWindow()->DC.CursorMaxPos.y -= yOffset;
                        }

                        I::TableNextColumn();
                        if (first && value.ObjectsSorted.size() > 1)
                        {
                            if (yOffset)
                                I::SetCursorPosY(I::GetCursorPosY() + yOffset);
                            if (scoped::WithID(&value))
                                if (I::Button(value.IsFolded ? ICON_FA_CHEVRON_RIGHT : ICON_FA_CHEVRON_DOWN))
                                    value.IsFolded ^= true;
                            I::GetCurrentWindow()->DC.CursorMaxPos.y -= yOffset;
                        }

                        I::TableNextColumn();
                        Controls::ContentButton(object, object);
                        if (first)
                        {
                            keyDrawn.emplace(key);
                            first = false;
                        }
                        ++drawn;
                    }
                    offset += numToDisplay;
                }
            }
        }
    }
};

}

export namespace GW2Viewer::G::Windows { UI::Windows::ListContentValues ListContentValues; }
