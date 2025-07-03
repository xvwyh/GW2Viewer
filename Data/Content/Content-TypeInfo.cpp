#include "UI/ImGui/ImGui.h"
#include "Utils.h"

module GW2Viewer.Data.Content;
import :ContentObject;
import :Symbols;
import GW2Viewer.Common;
import GW2Viewer.Common.JSON;
import GW2Viewer.Data.Game;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.String;
import std;

namespace GW2Viewer::Data::Content
{

STATIC(TypeInfo::Symbol::CurrentContext) { };

std::strong_ordering TypeInfo::SymbolType::CompareDataForSearch(byte const* dataA, byte const* dataB) const
{
    std::span const a { dataA, Size() };
    std::span const b { dataB, Size() };
    return std::lexicographical_compare_three_way(a.rbegin(), a.rend(), b.rbegin(), b.rend());
}

TypeInfo::Enum* TypeInfo::Symbol::GetEnum()
{
    if (!Enum)
        return nullptr;
    if (!Enum->Name.empty())
        if (auto const itr = G::Config.SharedEnums.find(Enum->Name); itr != G::Config.SharedEnums.end())
            return &itr->second;
    return &*Enum;
}

TypeInfo::StructLayout& TypeInfo::Symbol::GetElementLayout()
{
    if (!ElementTypeName.empty())
        if (auto const itr = G::Config.SharedTypes.find(ElementTypeName); itr != G::Config.SharedTypes.end())
            return itr->second;
    return ElementLayout;
}

std::optional<TypeInfo::Condition::ValueType> TypeInfo::Symbol::GetValueForCondition(ContentObject const& content, LayoutStack const& layoutStack) const
{
    if (!Condition || Condition->Field.empty())
        return { };

    /*
    // Doesn't work as intended, can only check for either current type field or the root type field
    auto const parts = split(Condition->Field, "../");//"->");
    LayoutStack layoutStackCopy = layoutStack;
    //while (layoutStackCopy.size() > parts.size() + layoutStack.top().ObjectStackDepth)
    for (size_t i = 1; i < parts.size(); ++i)
        layoutStackCopy.pop();
    */

    auto const& top = layoutStack.top();
    if (auto const itr = std::ranges::find_if(top.Layout->Symbols, [field = /*parts.back()*/Condition->Field](auto const& pair) { return pair.second.Name == field; }); itr != top.Layout->Symbols.end())
        if (auto const value = itr->second.GetType()->GetValueForCondition(&top.Content->Data[top.DataStart + itr->first]))
            return value;

    return { };
}
bool TypeInfo::Symbol::TestCondition(ContentObject const& content, LayoutStack const& layoutStack) const
{
    if (!Condition || Condition->Field.empty())
        return true;

    if (auto const value = GetValueForCondition(content, layoutStack))
        return Condition->Test(*value);

    return false;
}
void TypeInfo::Symbol::DrawOptions(TypeInfo& typeInfo, LayoutStack const& layoutStack, std::string_view parentPath, bool create, std::string const& placeholderName)
{
    // Name
    auto const oldFullPath = GetFullPath(parentPath);
    bool nameChanged = I::InputTextWithHint("Name", placeholderName.c_str(), &Name);
    if (!create && Name.empty())
    {
        Name = placeholderName;
        nameChanged = true;
    }
    auto const fullPath = GetFullPath(parentPath);
    if (nameChanged && fullPath != oldFullPath)
    {
        auto fixField = [oldFullPath, fullPath, oldPrefix = std::format("{}->", oldFullPath)](std::string& field)
        {
            if (field == oldFullPath)
                return field = fullPath, true;
            if (field.starts_with(oldPrefix))
                return field = std::format("{}->{}", fullPath, field.substr(oldPrefix.length())), true;
            return false;
        };
        std::ranges::for_each(typeInfo.NameFields, fixField);
        std::ranges::for_each(typeInfo.IconFields, fixField);
        std::ranges::for_each(typeInfo.MapFields, fixField);
    }

    // Type
    {
        static auto const& types = Symbols::GetTypes();
        int type = std::distance(types.begin(), std::ranges::find(types, GetType()));
        I::Combo("Type", &type, [](void*, int index) { return types[index]->Name; }, nullptr, std::size(types));
        Type = types[type]->Name;
    }

    // ElementTypeName
    {
        if (!ElementTypeShared)
            ElementTypeShared = !ElementTypeName.empty() && G::Config.SharedTypes.contains(ElementTypeName);
        if (I::Checkbox("Shared", &*ElementTypeShared) && !ElementTypeName.empty())
        {
            if (*ElementTypeShared && !G::Config.SharedTypes.contains(ElementTypeName))
                G::Config.SharedTypes.emplace(ElementTypeName, std::exchange(ElementLayout, StructLayout()));
            else if (G::Config.SharedTypes.contains(ElementTypeName))
                ElementLayout = G::Config.SharedTypes.at(std::exchange(ElementTypeName, std::string()));
        }
        I::SameLine();
        if (*ElementTypeShared)
        {
            if (auto items = [this](int index) { return index >= 0 && index < G::Config.SharedTypes.size() ? std::next(G::Config.SharedTypes.begin(), index)->first.c_str() : "<c=#4>Anonymous Inline Type</c>"; };
                scoped::Combo("ElementTypeName", items(ElementTypeName.empty() ? -1 : std::distance(G::Config.SharedTypes.begin(), G::Config.SharedTypes.find(ElementTypeName))), ImGuiComboFlags_HeightLarge))
            {
                if (I::ComboItem(items(-1), ElementTypeName.empty()))
                    ElementTypeName.clear();
                for (auto const& [index, value] : G::Config.SharedTypes | std::views::keys | std::views::enumerate)
                    if (I::ComboItem(items(index), ElementTypeName == value))
                        ElementTypeName = value;
            }
        }
        else
            I::InputText("ElementTypeName", &ElementTypeName);
    }

    // Alignment
    {
        static uint32 options[] { 0, 1, 2, 4, 8, 16, 24, 32, 40, 48, 56, 64 };
        static std::vector optionsStrings { std::from_range, options | std::views::transform([](uint32 alignment) { return std::array { std::format("Default ({} bytes)", alignment), std::format("{} bytes", alignment) }; }) };
        if (auto items = [this](int alignment) { return optionsStrings[std::distance(std::begin(options), std::ranges::find(options, alignment ? alignment : GetType()->Alignment()))][(bool)alignment].c_str(); };
            scoped::Combo("Alignment", items(Alignment), ImGuiComboFlags_HeightLarge))
            for (auto const alignment : options)
                if (I::ComboItem(items(alignment), Alignment == alignment))
                    Alignment = alignment;
    }

    std::vector<std::tuple<char const*, char const*, std::function<void()>>> extraSettings;
    auto booleanToggle = [&](bool& enabled, char const* icon, char const* name, char const* tooltip = nullptr, std::function<void()>&& settings = nullptr)
    {
        I::CheckboxButton(icon, enabled, tooltip ? tooltip : name, { I::GetFrameHeight(), I::GetFrameHeight() });
        I::SameLine(0, 0);
        if (settings && enabled)
            extraSettings.emplace_back(icon, name, std::move(settings));
    };
    auto optionalToggle = [&](auto& optional, char const* icon, char const* name, char const* tooltip = nullptr, std::function<void()>&& settings = nullptr)
    {
        bool enabled = optional.has_value();
        if (I::CheckboxButton(icon, enabled, tooltip ? tooltip : name, { I::GetFrameHeight(), I::GetFrameHeight() }))
        {
            if (enabled)
                optional.emplace();
            else
                optional.reset();
        }
        I::SameLine(0, 0);
        if (settings && enabled)
            extraSettings.emplace_back(icon, name, std::move(settings));
    };
    auto pathListToggle = [&](auto& pathList, char const* icon, char const* name, char const* tooltip = nullptr)
    {
        bool enabled = !Name.empty() && std::ranges::contains(pathList, fullPath);
        if (scoped::Disabled(Name.empty()))
        if (I::CheckboxButton(icon, enabled, tooltip ? tooltip : name, { I::GetFrameHeight(), I::GetFrameHeight() }))
        {
            if (enabled)
                pathList.emplace_back(fullPath);
            else
                std::erase(pathList, fullPath);
        }
        I::SameLine(0, 0);
        if (enabled)
        {
            extraSettings.emplace_back(icon, name, [&]
            {
                //if (scoped::WithStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0, I::GetStyle().ButtonTextAlign.y }))
                for (auto const& [index, path] : pathList | std::views::enumerate)
                {
                    //I::Button(std::format("<c=#{}>{}</c>", path == fullPath ? "F" : "8", path).c_str(), { -FLT_MIN, 0 });
                    I::Selectable(path.c_str(), path == fullPath);
                    if (I::IsItemHovered())
                        I::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

                    auto const rect = I::GetCurrentContext()->LastItemData.Rect;
                    int const dragging = I::GetMousePos().y < rect.Min.y ? -1 : I::GetMousePos().y >= rect.Max.y ? 1 : 0;
                    if (I::IsItemActive() && !I::IsItemHovered() && dragging)
                        if (int const next = index + dragging; next >= 0 && next < pathList.size())
                            std::swap(pathList[index], pathList[next]);
                }
            });
        }
    };

    optionalToggle(Condition, ICON_FA_SEAL_QUESTION, "Condition", "Conditional Field", [&]
    {
        std::vector fields(std::from_range, layoutStack.top().Layout->Symbols | std::views::transform([](auto const& pair) { return pair.second.Name; }));
        /*
        auto layoutStackCopy = layoutStack;
        //SymbolMap const* previousLayout = nullptr;
        while (!layoutStackCopy.empty())
        {
            LayoutFrame const& frame = layoutStackCopy.top();

            // Doesn't work as intended, can only check for either current type field or the root type field
            //if (previousLayout)
            //    if (auto itr = std::ranges::find_if(*frame.Layout, [previousLayout](auto const& pair) { return &pair.second.ElementLayout.Symbols == previousLayout; }); itr != frame.Layout->end())
            //        for (auto& field : fields)
            //            field = std::format("{}->{}", itr->second.Name, field);
            //if (previousLayout)
            //    for (auto& field : fields)
            //        field = std::format("->{}", field);
            for (auto const& symbol : *frame.Layout | std::views::values)
                fields.emplace_back(std::format("{}{}", std::views::repeat(std::string("../"), layoutStack.size() - layoutStackCopy.size()) | std::views::join_with(std::string()) | std::ranges::to<std::string>(), symbol.Name));

            //fields.append_range(*frame.Layout | std::views::transform([](auto const& pair) { return pair.second.Name; }));
            //previousLayout = frame.Layout;
            layoutStackCopy.pop();
        }
        */
        auto const itr = std::ranges::find(fields, Condition->Field);
        int index = itr != fields.end() ? std::distance(fields.begin(), itr) : -1;
        if (I::Combo("Field", &index, [](void* fields, int index) { return (*(std::vector<std::string>*)fields)[index].c_str(); }, &fields, fields.size()) && index >= 0)
            Condition->Field = fields[index];

        I::Combo("Comparison", (int*)&Condition->Comparison, Condition::IMGUI_COMPARISONS);
        I::InputScalar("Value", ImGuiDataType_S64, &Condition->Value);
    });
    optionalToggle(Enum, ICON_FA_LIST_OL, "Enum", "Display as Enum", [&]
    {
        {
            if (!EnumShared)
                EnumShared = !Enum->Name.empty() && G::Config.SharedEnums.contains(Enum->Name);
            if (I::Checkbox("Shared", &*EnumShared) && !Enum->Name.empty())
            {
                if (*EnumShared && !G::Config.SharedEnums.contains(Enum->Name))
                    G::Config.SharedEnums.emplace(Enum->Name, std::exchange(*Enum, TypeInfo::Enum { .Name = Enum->Name }));
                else
                {
                    if (G::Config.SharedEnums.contains(Enum->Name))
                        *Enum = G::Config.SharedEnums.at(Enum->Name);
                    Enum->Name.clear();
                }
            }
            I::SameLine();
            if (*EnumShared)
            {
                if (auto items = [this](int index) { return index >= 0 && index < G::Config.SharedEnums.size() ? std::next(G::Config.SharedEnums.begin(), index)->first.c_str() : "<c=#4>Anonymous Enum</c>"; };
                    scoped::Combo("##Name", items(Enum->Name.empty() ? -1 : std::distance(G::Config.SharedEnums.begin(), G::Config.SharedEnums.find(Enum->Name))), ImGuiComboFlags_HeightLarge))
                {
                    if (I::ComboItem(items(-1), Enum->Name.empty()))
                        Enum->Name.clear();
                    for (auto const& [index, value] : G::Config.SharedEnums | std::views::keys | std::views::enumerate)
                        if (I::ComboItem(items(index), Enum->Name == value))
                            Enum->Name = value;
                }
            }
            else
                I::InputTextWithHint("##Name", "Enum Name", &Enum->Name);
        }

        auto const e = GetEnum();

        I::Checkbox("Flags", &e->Flags);
        
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("Values", 5, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY, { 0, std::min(200.0f, I::GetFrameHeight() * (e->Values.size() + 1)) }))
        {
            I::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("ShiftBack", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("ShiftForward", ImGuiTableColumnFlags_WidthFixed);
            std::optional<Enum::UnderlyingType> remove;
            for (auto& [value, name] : e->Values)
            {
                scoped::WithID(&value);
                auto valueCopy = value;
                I::TableNextRow();

                I::TableNextColumn();
                if (e->Flags)
                {
                    I::AlignTextToFramePadding();
                    I::Text("0x");
                    I::SameLine(0, 0);
                }
                I::SetNextItemWidth(e->Flags ? 100 : 50);
                I::InputScalar("##InputEnumValue", e->Flags ? ImGuiDataType_U64 : ImGuiDataType_S64, &valueCopy, nullptr, nullptr, e->GetFormat(), ImGuiInputTextFlags_ReadOnly);

                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                I::InputText("##InputEnumName", &name);
                if (I::TableNextColumn(); I::Button(ICON_FA_MINUS "##RemoveEnum", { I::GetFrameHeight(), I::GetFrameHeight() }))
                    remove.emplace(value);
                if (I::TableNextColumn(); e->Flags ? value > 1 && !e->Values.contains(value >> 1) : value > 0 && !e->Values.contains(value - 1))
                    if (I::Button(ICON_FA_ARROW_UP_FROM_LINE "##ShiftBackEnum", { I::GetFrameHeight(), I::GetFrameHeight() }))
                        for (auto itr = e->Values.begin(); itr != e->Values.end(); ++itr)
                            if (itr->first >= value)
                            {
                                auto node = e->Values.extract(itr);
                                auto& v = node.key();
                                v = e->Flags ? v >> 1 : v - 1;
                                e->Values.insert(std::move(node));
                            }
                if (I::TableNextColumn(); true)
                    if (I::Button(ICON_FA_ARROW_DOWN_FROM_LINE "##ShiftForwardEnum", { I::GetFrameHeight(), I::GetFrameHeight() }))
                        for (auto itr = e->Values.rbegin(); itr != e->Values.rend(); ++itr)
                            if (itr->first >= value)
                            {
                                auto node = e->Values.extract(--itr.base());
                                auto& v = node.key();
                                v = e->Flags ? v << 1 : v + 1;
                                e->Values.insert(std::move(node));
                            }
            }
            if (remove)
                e->Values.erase(*remove);

            static Enum::UnderlyingType value;
            static std::string name;
            I::TableNextRow();
            if (scoped::WithColorVar(ImGuiCol_Text, 0x80FFFFFF))
            {
                I::TableNextColumn();
                if (e->Flags)
                {
                    I::AlignTextToFramePadding();
                    I::Text("0x");
                    I::SameLine(0, 0);
                }
                I::SetNextItemWidth(e->Flags ? 100 : 50);
                I::InputScalar("##InputEnumValue", e->Flags ? ImGuiDataType_U64 : ImGuiDataType_S64, &value, nullptr, nullptr, e->GetFormat(), e->Flags ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_CharsDecimal);

                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                I::InputText("##InputEnumName", &name);
            }
            if (I::TableNextColumn(); I::Button(ICON_FA_PLUS "##AddEnum", { I::GetFrameHeight(), I::GetFrameHeight() }))
            {
                e->Values.emplace(value, name);
                value = e->Flags ? value << 1 : value + 1;
                name.clear();
            }
        }
    });
    booleanToggle(Table, ICON_FA_TABLE, "Table", "Display as Table");
    pathListToggle(typeInfo.NameFields, ICON_FA_FONT_CASE, "Name", "Display as Content Name");
    pathListToggle(typeInfo.IconFields, ICON_FA_IMAGE, "Icon", "Display as Content Icon");
    pathListToggle(typeInfo.MapFields, ICON_FA_GLOBE, "Map", "Use as Map Source");

    I::NewLine();
    if (!extraSettings.empty())
    {
        if (scoped::Child("ExtraSettingsContainer", { }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
        if (scoped::TabBar("ExtraSettings", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_NoTabListScrollingButtons))
            for (auto const& [icon, name, settings] : extraSettings)
                if (scoped::TabItem(std::format("{} {}", icon, name).c_str()))
                    settings();
    }
}
void TypeInfo::Symbol::Draw(byte const* data, DrawType draw, ContentObject& content)
{
    if (draw == DrawType::TableCountColumns)
        return;

    CurrentContext =
    {
        .Draw = draw,
        .Content = &content
    };

    auto* type = GetType();
    std::string typeName = type->Name;
    if (auto const contentPtr = type->GetContent(data))
    {
        if (auto const content = *contentPtr)
            Utils::String::ReplaceAll(typeName, "Content", Utils::Encoding::ToUTF8(content->Type->GetDisplayName()));
        else if (!ElementTypeName.empty())
            Utils::String::ReplaceAll(typeName, "T", ElementTypeName);
    }
    else if (!ElementTypeName.empty())
        Utils::String::ReplaceAll(typeName, "T", ElementTypeName);
    else if (Enum && !Enum->Name.empty())
        typeName = Enum->Name;

    switch (draw)
    {
        case DrawType::TableCountColumns:
            std::terminate();
        case DrawType::TableHeader:
        {
            auto const rect = I::GetCurrentContext()->LastItemData.Rect;
            I::AlignTextToFramePadding();
            I::SetCursorPosY(I::GetCursorPosY() + I::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
            I::TableHeader(I::TableGetColumnName());
            I::SetCursorPosX(I::GetCursorPosX() + rect.GetWidth());
            I::TextColored({ 1, 1, 1, 0.25f }, "%s ", typeName.c_str());
            break;
        }
        case DrawType::Inline:
            for (auto const part : { G::Config.ShowContentSymbolNameBeforeType, !G::Config.ShowContentSymbolNameBeforeType })
            {
                I::AlignTextToFramePadding();
                switch (part)
                {
                    case false: I::TextColored({ 1, 1, 1, 0.25f }, "%s ", typeName.c_str()); break;
                    case true: I::TextColored({ 1, 1, 1, 0.5f }, draw == DrawType::Inline ? "%s = " : "%s", Name.c_str()); break;
                }
                I::SameLine(0, 0);
            }
            [[fallthrough]];
        case DrawType::TableRow:
            if (scoped::WithID(data))
                type->Draw(data, *this);
            break;
    }
}
uint32 TypeInfo::Symbol::Size() const
{
    return GetType()->Size();
}
uint32 TypeInfo::Symbol::AlignedSize() const
{
    auto const alignment = Alignment ? Alignment : GetType()->Alignment();
    return (Size() + alignment - 1) / alignment * alignment;
}
TypeInfo::SymbolType const* TypeInfo::Symbol::GetType() const
{
    assert(!Type.empty());
    return Symbols::GetByName(Type);
}

void to_json(ordered_json& json, TypeInfo::StructLayout const& self)
{
    for (auto const& [offset, symbol] : self.Symbols | std::views::filter([](auto const& pair) { return !pair.second.Autogenerated; }))
    {
        auto& j = json.emplace_back();
        j["Offset"] = offset;
        to_json(j, symbol);
    }
}
void from_json(ordered_json const& json, TypeInfo::StructLayout& self)
{
    for (auto const& symbol : json)
        self.Symbols.emplace(symbol.value("Offset", 0), symbol.get<TypeInfo::Symbol>());
}

void TypeInfo::Initialize(ContentTypeInfo const& typeInfo)
{
    if (!Layout.Autogenerated)
    {
        Layout.Autogenerated = true;
        auto& layout = Layout.Symbols;
        if (typeInfo.GUIDOffset >= 0)
            layout.emplace(typeInfo.GUIDOffset, Symbol { .Name = "Content::GUID", .Type = "GUID", .Autogenerated = true });
        if (typeInfo.UIDOffset >= 0)
            layout.emplace(typeInfo.UIDOffset, Symbol { .Name = "Content::UID", .Type = "uint32", .Autogenerated = true });
        if (typeInfo.DataIDOffset >= 0)
            layout.emplace(typeInfo.DataIDOffset, Symbol { .Name = "Content::DataID", .Type = "uint32", .Autogenerated = true });
        if (typeInfo.NameOffset >= 0)
        {
            layout.emplace(typeInfo.NameOffset, Symbol { .Name = "Content::Name", .Type = "wchar_t**", .Autogenerated = true });
            layout.emplace(typeInfo.NameOffset + sizeof(void*), Symbol { .Name = "Content::FullName", .Type = "wchar_t**", .Autogenerated = true });
        }
        if (typeInfo.GUIDOffset == 0 && typeInfo.UIDOffset == sizeof(GUID) + sizeof(uint32))
            layout.emplace(typeInfo.GUIDOffset + sizeof(GUID), Symbol { .Name = "Content::Type", .Type = "uint32", .Autogenerated = true });
    }
}

}
