module;
#include "UI/ImGui/ImGui.h"

module GW2Viewer.Data.Content;
import :ContentObject;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.String;
import std;

namespace GW2Viewer::Data::Content::Symbols
{

static auto& context = TypeInfo::Symbol::CurrentContext;

TypeInfo::SymbolType const* GetByName(std::string_view name)
{
    return *std::ranges::find_if(GetTypes(), [name](auto* type) { return type->Name == name; });
}

template<typename T> void Integer<T>::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    auto text = GetDisplayText(data);
    if (auto const e = symbol.GetEnum())
    {
        auto value = *(T const*)data;
        if (e->Flags)
        {
            text.clear();
            TypeInfo::Enum::FlagsUnderlyingType remaining = value;
            for (auto const& [flag, name] : e->Values | std::views::reverse)
            {
                if (TypeInfo::Enum::FlagsUnderlyingType const flagTyped = flag; (remaining & flagTyped) == flagTyped)
                {
                    text = std::format("{} | {}", name, text);
                    remaining &= ~flagTyped;
                }
            }
            if (remaining)
                text = std::format("{}0x{:X}", text, remaining);
            else if (auto const trimmed = text.find_last_not_of(" |"); trimmed != std::string::npos)
                text = text.substr(0, trimmed + 1);

            text = std::format("{}   <c=#4>(0x{:X})</c>", text, value);
        }
        else
        {
            if (auto itr = e->Values.find(value); itr != e->Values.end())
                text = !e->Name.empty() ? std::format("<c=#4>{}::</c>{}   <c=#4>({})</c>", e->Name, itr->second, value) : std::format("{}   <c=#4>({})</c>", itr->second, value);
            else if (!e->Name.empty())
                text = std::format("<c=#4>({})</c>{}   <c=#4>({})</c>", e->Name, value, value);
        }
    }
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", text);
}

template<typename T> void Number<T>::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", GetDisplayText(data));
}

template<typename T> std::strong_ordering String<T>::CompareDataForSearch(byte const* dataA, byte const* dataB) const
{
    if (GetStruct(dataA).Hash != GetStruct(dataB).Hash)
        return GetStringView(dataA) <=> GetStringView(dataB);
    return std::strong_ordering::equal;
}
template<typename T> std::string String<T>::GetDisplayText(byte const* data) const
{
    try
    {
        auto text = GetString(data);
        if constexpr (IsWide)
        {
            Utils::String::ReplaceAll(text, L"\r", LR"(\r)");
            Utils::String::ReplaceAll(text, L"\n", LR"(\n)");
            return Utils::Encoding::ToUTF8(text);
        }
        else
        {
            Utils::String::ReplaceAll(text, "\r", R"(\r)");
            Utils::String::ReplaceAll(text, "\n", R"(\n)");
            return text;
        }
    }
    catch (std::exception const& ex)
    {
        return ex.what();
    }
}
template<typename T> void String<T>::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", GetDisplayText(data));
}

template<typename T> std::string StringPointer<T>::GetDisplayText(byte const* data) const
{
    if (auto const target = (typename String<T>::Struct const* const*)data; *target)
        return GetTargetSymbolType()->GetDisplayText((byte const*)*target);
    return "";
}
template<typename T> void StringPointer<T>::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    if (auto const target = (typename String<T>::Struct const* const*)data; *target)
        GetTargetSymbolType()->Draw((byte const*)*target, symbol);
}

std::string Color::GetDisplayText(byte const* data) const
{
    union
    {
        uint32 Color;
        byte Channels[4];
    } const original { .Color = *(uint32 const*)data };
    auto color = original;
    for (auto const& [dest, source] : Swizzle | std::views::enumerate)
        color.Channels[dest] = original.Channels[source];
    return std::format("<c=#{0:08X}>#{0:08X}</c>", std::byteswap(color.Color));
}
void Color::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    ImVec4 const original = I::ColorConvertU32ToFloat4(*(uint32 const*)data); // 0xAABBGGRR
    ImVec4 color;
    for (auto const& [dest, source] : Swizzle | std::views::enumerate)
        ((float*)&color)[dest] = ((float const*)&original)[source];
    I::SetNextItemWidth(200);
    I::ColorEdit4("##Input", &color.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
}

static constexpr std::array pointColors { 0xFFCCCCFF, 0xFFCCFFCC, 0xFFFFCCCC, 0xFFFFCCFF };
template<typename T, size_t N> std::string Point<T, N>::GetDisplayText(byte const* data) const
{
    return std::format("({})", std::string { std::from_range, std::views::zip_transform([](T value, uint32 color) { return std::format("<c=#{:X}>{}</c>", std::byteswap(color), value); }, std::span { (T const*)data, N }, pointColors) | std::views::join_with(std::string(", ")) });
}
template<typename T, size_t N> void Point<T, N>::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    for (auto const& [index, value, color] : std::views::zip(std::views::iota(0), std::span { (T const*)data, N }, pointColors))
    {
        if (index)
            I::SameLine(0, 0);
        if (scoped::WithColorVar(ImGuiCol_Text, I::ColorConvertU32ToFloat4(color)))
            I::InputTextReadOnly(std::format("##Input{}", index).c_str(), std::format("{}", value));
    }

    I::SameLine(0, 0);
    if (I::Button(ICON_FA_GLOBE))
        ; // TOOD: Open world map to world-space coods
    if (auto const map = context.Content->GetMap(); map && map != context.Content)
    {
        I::SameLine(0, 0);
        if (I::Button(std::format(ICON_FA_LOCATION_DOT " <c=#4>in</c> <c=#8>{}</c> {}", map->Type->GetDisplayName(), map->GetDisplayName()).c_str()))
            ; // TODO: Open world map to map-space coords
    }
}

std::string GUID::GetDisplayText(byte const* data) const
{
    if (auto const* object = *GetContent(data))
        return Utils::Encoding::ToUTF8(object->GetDisplayName(false, true));
    return std::format("{}", *(GW2Viewer::GUID const*)data);
}
std::optional<uint32> GUID::GetIcon(byte const* data) const
{
    if (auto const* object = *GetContent(data))
        return object->GetIcon();
    return { };
}
std::optional<ContentObject*> GUID::GetMap(byte const* data) const
{
    if (auto const* object = *GetContent(data))
        return object->GetMap();
    return { };
}
std::optional<ContentObject*> GUID::GetContent(byte const* data) const
{
    if (auto const object = G::Game.Content.GetByGUID(*(GW2Viewer::GUID const*)data))
    {
        object->Finalize();
        return object;
    }
    return nullptr;
}
void GUID::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    I::InputTextReadOnly("##Input", std::format("{}", *(GW2Viewer::GUID const*)data));

    if (auto* object = *GetContent(data); object && object != context.Content)
    {
        I::SameLine(0, 0);
        UI::Controls::ContentButton(object, object);
    }
}

std::strong_ordering Token32::CompareDataForSearch(byte const* dataA, byte const* dataB) const
{
    auto const a = GetDecoded(dataA);
    auto const b = GetDecoded(dataB);
    return std::string_view(a.data(), a.size() - 1) <=> std::string_view(b.data(), b.size() - 1);
}
void Token32::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", GetDecoded(data).data());
}

std::strong_ordering Token64::CompareDataForSearch(byte const* dataA, byte const* dataB) const
{
    auto const a = GetDecoded(dataA);
    auto const b = GetDecoded(dataB);
    return std::string_view(a.data(), a.size() - 1) <=> std::string_view(b.data(), b.size() - 1);
}
void Token64::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", GetDecoded(data).data());
}

std::string StringID::GetDisplayText(byte const* data) const
{
    auto const stringID = GetStringID(data);
    auto [string, status] = G::Game.Text.Get(stringID);
    return std::format("{}{}", Encryption::GetStatusText(status), string ? *string : L"");
}
void StringID::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    uint32 stringID = GetStringID(data);
    I::SetNextItemWidth(60);
    I::InputTextReadOnly("##InputID", std::format("{}", stringID));

    I::SameLine(0, 0);
    UI::Controls::TextVoiceButton(stringID);

    auto text = GetDisplayText(data);
    I::SameLine(0, 0);
    if (scoped::WithCursorOffset({ })) // Reserve space in tables
        I::ItemSize({ 100, 0 });
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    if (text.contains('\n'))
        I::InputTextMultiline("##Input", &text, { }, ImGuiInputTextFlags_ReadOnly);
    else
        I::InputTextReadOnly("##Input", text);
}

void FileID::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    auto const fileID = GetFileID(data);
    I::SetNextItemWidth(70);
    I::InputTextReadOnly("##Input", std::format("{}", fileID));

    I::SameLine(0, 0);
    I::Button("<FILE>##Preview");
    if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
        if (auto* file = G::Game.Archive.GetFileEntry(fileID))
            UI::Viewers::FileViewer::Open(*file, { .MouseButton = button });
    if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
        UI::Controls::Texture(fileID);
}

void RawPointerT::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    auto* ptr = *GetPointer(data);
    if (!ptr)
        I::PushStyleColor(ImGuiCol_Text, 0x40FFFFFF);

    if (!ptr || G::Config.ShowValidRawPointers)
        I::TextColored({ 0.75f, 0.75f, 1.0f, ptr ? 1.0f : 0.25f }, "0x%llX", ptr), I::SameLine();
    I::Text(ICON_FA_ARROW_RIGHT " %s", !symbol.ElementTypeName.empty() ? symbol.ElementTypeName.c_str() : "T");
    I::SameLine();
    I::SetNextItemWidth(120);
    I::DragCoerceInt("##ElementSize", (int*)&symbol.ElementSize, 0.1f, 1, 10000, "Size: %u bytes", ImGuiSliderFlags_AlwaysClamp, [](auto v) { return std::max(1, (v / 4) * 4); });
    if (I::IsItemHovered())
        I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (!ptr)
        I::PopStyleColor();
}

std::string ContentPointer::GetDisplayText(byte const* data) const
{
    if (auto const* object = *GetContent(data))
        return Utils::Encoding::ToUTF8(object->GetDisplayName(false, true));
    return { };
}
std::optional<uint32> ContentPointer::GetIcon(byte const* data) const
{
    if (auto const* object = *GetContent(data))
        return object->GetIcon();
    return { };
}
std::optional<ContentObject*> ContentPointer::GetMap(byte const* data) const
{
    if (auto const* object = *GetContent(data))
        return object->GetMap();
    return { };
}
std::optional<ContentObject*> ContentPointer::GetContent(byte const* data) const
{
    if (auto const object = G::Game.Content.GetByDataPointer(*GetPointer(data)))
    {
        object->Finalize();
        return object;
    }
    return nullptr;
}
void ContentPointer::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    auto* ptr = *GetPointer(data);
    auto* object = *GetContent(data);
    if (!ptr)
        I::PushStyleColor(ImGuiCol_Text, 0x40FFFFFF);
    else if (!object)
        I::PushStyleColor(ImGuiCol_Text, 0xFF0000FF);

    if (ptr && !object)
        I::Text("0x%llX", ptr), I::SameLine();
    else if (!ptr || G::Config.ShowValidRawPointers)
        I::TextColored({ 0.75f, 0.75f, 1.0f, ptr ? 1.0f : 0.25f }, "0x%llX", ptr), I::SameLine();

    if (ptr)
        UI::Controls::ContentButton(object, ptr, { .MissingContentName = ptr ? "CONTENT OBJECT MISSING" : "" });
    else
        I::Dummy({ });

    if (!ptr || !object)
        I::PopStyleColor();
}

void ArrayT::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    auto* ptr = *GetPointer(data);
    if (!ptr)
        I::PushStyleColor(ImGuiCol_Text, 0x40FFFFFF);

    if (!ptr || G::Config.ShowValidRawPointers)
        I::TextColored({ 0.75f, 0.75f, 1.0f, ptr ? 1.0f : 0.25f }, "0x%llX", ptr), I::SameLine();
    I::Text(ICON_FA_ARROW_RIGHT " %s[%u]", !symbol.ElementTypeName.empty() ? symbol.ElementTypeName.c_str() : "T", *GetArrayCount(data));
    I::SameLine();
    I::SetNextItemWidth(120);
    I::DragCoerceInt("##ElementSize", (int*)&symbol.ElementSize, 0.1f, 1, 10000, "Element: %u bytes", ImGuiSliderFlags_AlwaysClamp, [](auto v) { return std::max(1, (v / 4) * 4); });
    if (I::IsItemHovered())
        I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    I::SameLine();
    I::Text("Total: %u bytes", *GetArrayCount(data) * symbol.ElementSize);

    if (!ptr)
        I::PopStyleColor();
}

std::optional<ContentObject*> ArrayContent::GetContent(byte const* data) const
{
    if (auto const object = G::Game.Content.GetByDataPointer(*GetPointer(data)))
    {
        object->Finalize();
        return object;
    }
    return nullptr;
}
void ArrayContent::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    auto* ptr = *GetPointer(data);
    auto const* object = *GetContent(data);
    if (!ptr)
        I::PushStyleColor(ImGuiCol_Text, 0x40FFFFFF);
    else if (!object)
        I::PushStyleColor(ImGuiCol_Text, 0xFF0000FF);

    if (ptr && !object)
        I::Text("0x%llX", ptr), I::SameLine();
    else if (!ptr || G::Config.ShowValidRawPointers)
        I::TextColored({ 0.75f, 0.75f, 1.0f, ptr ? 1.0f : 0.25f }, "0x%llX", ptr), I::SameLine();

    I::Text(ICON_FA_ARROW_RIGHT " %s[%u]", object ? Utils::Encoding::ToUTF8(object->Type->GetDisplayName()).c_str() : "Content", *GetArrayCount(data));
    I::SameLine();
    I::SetNextItemWidth(120);
    I::DragCoerceInt("##ElementSize", (int*)&symbol.ElementSize, 0.1f, 1, 10000, "Element: %u bytes", ImGuiSliderFlags_AlwaysClamp, [](auto v) { return std::max(1, (v / 4) * 4); });
    if (I::IsItemHovered())
        I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    I::SameLine();
    I::Text("Total: %u bytes", *GetArrayCount(data) * symbol.ElementSize);

    if (!ptr || !object)
        I::PopStyleColor();
}

std::tuple<TypeInfo::SymbolType const*, uint32> GetSymbolTypeForContentType(GW2Viewer::Content::EContentTypes type)
{
    switch (type)
    {
        using enum GW2Viewer::Content::EContentTypes;
        case CONTENT_TYPE_BOOLEAN: return { GetByName("bool"), 1 };
        case CONTENT_TYPE_ENUM: return { GetByName("int8"), 1 };
        case CONTENT_TYPE_FLAGS: return { GetByName("int16"), 1 };
        case CONTENT_TYPE_TIME:
        case CONTENT_TYPE_INTEGER: return { GetByName("int32"), 1 };
        case CONTENT_TYPE_INTEGER_PAIR:
        case CONTENT_TYPE_INTEGER_RANGE: return { GetByName("int32"), 2 };
        case CONTENT_TYPE_NUMBER: return { GetByName("float"), 1 };
        case CONTENT_TYPE_NUMBER_PAIR:
        case CONTENT_TYPE_NUMBER_RANGE: return { GetByName("float"), 2 };
        case CONTENT_TYPE_POINT_3D: return { GetByName("float"), 3 };
        case CONTENT_TYPE_VARIABLE:
        case CONTENT_TYPE_STRING: return { GetByName("wchar_t*"), 1 };
        case CONTENT_TYPE_TOKEN32: return { GetByName("Token32"), 1 };
        case CONTENT_TYPE_TOKEN64: return { GetByName("Token64"), 1 };
        default: return { type < CONTENT_TYPES_BASIC ? GetByName("Content*") : nullptr, 1 };
    }
}

std::strong_ordering ParamValue::CompareDataForSearch(byte const* dataA, byte const* dataB) const
{
    auto const& paramA = GetStruct(dataA);
    auto const& paramB = GetStruct(dataB);
    if (auto const result = paramA.ContentType <=> paramB.ContentType; result != std::strong_ordering::equal)
        return result;

    if (auto [typeInfo, count] = GetSymbolTypeForContentType(paramA.ContentType); typeInfo)
    {
        auto pA = paramA.Raw;
        auto pB = paramB.Raw;
        for (uint32 i = 0; i < count; ++i)
        {
            if (auto const result = typeInfo->CompareDataForSearch(pA, pB); result != std::strong_ordering::equal)
                return result;

            pA += typeInfo->Size();
            pB += typeInfo->Size();
        }
    }

    return std::strong_ordering::equal;
}

std::optional<TypeInfo::Condition::ValueType> ParamValue::GetValueForCondition(byte const* data) const
{
    auto const& param = GetStruct(data);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
        return typeInfo->GetValueForCondition(param.Raw);
    return { };
}
std::string ParamValue::GetDisplayText(byte const* data) const
{
    auto const& param = GetStruct(data);
    if (param.ContentType >= G::Game.Content.GetNumTypes())
        return std::format("<c=#F00>{}</c>", (uint32)param.ContentType);

    auto const typeName = std::format("<c=#8>{}</c> {}", ICON_FA_GEAR, G::Game.Content.GetType((uint32)param.ContentType)->GetDisplayName());
    switch (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); count)
    {
        default: return std::format("<c=#4>{}</c> {}", typeName, typeInfo ? typeInfo->GetDisplayText(param.Raw) : "<c=#F00>Unhandled basic type</c>");
        case 2: return std::format("<c=#4>{}</c> ({}, {})", typeName, typeInfo->GetDisplayText(param.Raw), typeInfo->GetDisplayText(&param.Raw[typeInfo->Size()]));
        case 3: return std::format("<c=#4>{}</c> ({}, {}, {})", typeName, typeInfo->GetDisplayText(param.Raw), typeInfo->GetDisplayText(&param.Raw[typeInfo->Size()]), typeInfo->GetDisplayText(&param.Raw[typeInfo->Size() * 2]));
    }
}
std::optional<uint32> ParamValue::GetIcon(byte const* data) const
{
    auto const& param = GetStruct(data);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
        return typeInfo->GetIcon(param.Raw);
    return { };
}
std::optional<ContentObject*> ParamValue::GetMap(byte const* data) const
{
    auto const& param = GetStruct(data);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
        return typeInfo->GetMap(param.Raw);
    return { };
}
void ParamValue::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    auto const& param = GetStruct(data);
    if (param.ContentType >= G::Game.Content.GetNumTypes())
    {
        I::Text("<c=#F00>%u</c>", (uint32)param.ContentType);
        return;
    }

    I::Text("<c=#4>%s</c>", Utils::Encoding::ToUTF8(G::Game.Content.GetType((uint32)param.ContentType)->GetDisplayName()).c_str());
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
    {
        auto p = param.Raw;
        for (uint32 i = 0; i < count; ++i)
        {
            I::SameLine();
            typeInfo->Draw(p, symbol);
            p += typeInfo->Size();
        }
    }
    else
    {
        I::SameLine();
        I::Text("<c=#F00>Unhandled basic type</c>");
    }
    if (param.GUID != GW2Viewer::GUID::Empty)
        GetByName("GUID")->Draw((byte const*)&param.GUID, symbol);
}

std::strong_ordering ParamDeclare::CompareDataForSearch(byte const* dataA, byte const* dataB) const
{
    auto const& paramA = GetStruct(dataA);
    auto const& paramB = GetStruct(dataB);
    if (paramA.Name.Hash != paramB.Name.Hash)
        if (auto const result = std::wstring_view(paramA.Name.Pointer) <=> std::wstring_view(paramB.Name.Pointer); result != std::strong_ordering::equal)
            return result;
    return GetByName("ParamValue")->CompareDataForSearch((byte const*)&paramA.Value, (byte const*)&paramB.Value);
}

std::optional<TypeInfo::Condition::ValueType> ParamDeclare::GetValueForCondition(byte const* data) const
{
    auto const& param = GetStruct(data);
    return GetByName("ParamValue")->GetValueForCondition((byte const*)&param.Value);
}
std::optional<uint32> ParamDeclare::GetIcon(byte const* data) const
{
    auto const& param = GetStruct(data);
    return GetByName("ParamValue")->GetIcon((byte const*)&param.Value);
}
std::optional<ContentObject*> ParamDeclare::GetMap(byte const* data) const
{
    auto const& param = GetStruct(data);
    return GetByName("ParamValue")->GetMap((byte const*)&param.Value);
}
void ParamDeclare::Draw(byte const* data, TypeInfo::Symbol& symbol) const
{
    auto const& param = GetStruct(data);
    if (scoped::Group())
    {
        I::InputTextReadOnly("##NameInput", GetByName("wchar_t*")->GetDisplayText((byte const*)&param.Name));
        I::SameLine();
        GetByName("ParamValue")->Draw((byte const*)&param.Value, symbol);
    }
}

std::vector<TypeInfo::SymbolType const*>& GetTypes()
{
    static std::vector<TypeInfo::SymbolType const*> instance
    {
        new Integer<bool>("bool"),
        new Integer<byte>("uint8"),
        new Integer<sbyte>("int8"),
        new Integer<uint16>("uint16"),
        new Integer<int16>("int16"),
        new Integer<uint32>("uint32"),
        new Integer<int32>("int32"),
        new Integer<uint64>("uint64"),
        new Integer<int64>("int64"),
        new Number<float>("float"),
        //new Number<double>("double"),
        new String<char>("char*"),
        new StringPointer<char>("char**"),
        new String<wchar_t>("wchar_t*"),
        new StringPointer<wchar_t>("wchar_t**"),
        new Color("ColorRGBA", { 0, 1, 2, 3 }),
        new Color("ColorBGRA", { 2, 1, 0, 3 }),
        new Color("ColorARGB", { 3, 0, 1, 2 }),
        new Color("ColorABGR", { 3, 2, 1, 0 }),
        new Point<float, 2>("Point2D"),
        new Point<float, 3>("Point3D"),
        new Point<int, 2>("IntPoint2D"),
        new Point<int, 3>("IntPoint3D"),
        new GUID(),
        new Token32(),
        new Token64(),
        new StringID(),
        new FileID(),
        new ContentPointer(),
        new ArrayContent(),
        new RawPointerT(),
        new ArrayT(),
        new ParamValue(),
        new ParamDeclare(),
    };
    return instance;
}

}
