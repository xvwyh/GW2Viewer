module GW2Viewer.Data.Content;
import :ContentObject;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.String;
import std;
#include "Macros.h"

namespace GW2Viewer::Data::Content::Symbols
{

TypeInfo::SymbolType const* GetByName(std::string_view name)
{
    return *std::ranges::find_if(GetTypes(), [name](auto* type) { return type->Name == name; });
}

template<typename T> std::string Integer<T>::GetDisplayText(Context const& context) const
{
    auto value = context.Data<T>();
    if (auto const e = context.Symbol.GetEnum())
    {
        if (e->Flags)
        {
            std::string text;
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

            return text;
        }

        if (auto itr = e->Values.find(value); itr != e->Values.end())
            return itr->second;
    }
    return std::format("{}", value);
}
template<typename T> ordered_json Integer<T>::Export(Context const& context, ExportOptions const& options) const
{
    auto const value = context.Data<T>();
    if (auto const e = context.Symbol.GetEnum())
    {
        if (e->Flags)
        {
            ordered_json json;
            // Workaround for std::make_unsigned not accepting bool type, because of the silly decision to implement bool symbols as Integer<bool>
            // flags will be the value converted to unsigned version of type T, to avoid e.g. int32 0x80000000 auto expanding to int64 as 0xFFFFFFFF80000000
            auto const flags = [value]
            {
                if constexpr (std::is_same_v<T, bool>)
                    return value;
                else
                    return (std::make_unsigned_t<T>)value;
            }();
            for (TypeInfo::Enum::FlagsUnderlyingType bit = 1; bit; bit <<= 1)
            {
                if (flags & bit)
                {
                    if (auto const itr = e->Values.find(bit); itr != e->Values.end())
                        json.emplace_back(itr->second);
                    else
                        json.emplace_back(std::format("0x{:X}", bit));
                }
            }

            return json;
        }

        if (auto itr = e->Values.find(value); itr != e->Values.end())
            return itr->second;

        return GetDisplayText(context);
    }
    return value;
}
template<typename T> void Integer<T>::Draw(Context const& context) const
{
    std::string text;
    if (auto const e = context.Symbol.GetEnum())
    {
        auto value = context.Data<T>();
        if (e->Flags)
        {
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
            else
                text = GetDisplayText(context);
        }
    }
    else
        text = GetDisplayText(context);
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", text);
}

template<typename T> void Number<T>::Draw(Context const& context) const
{
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", GetDisplayText(context));
}

template<typename T> std::strong_ordering String<T>::CompareDataForSearch(byte const* dataA, byte const* dataB) const
{
    if (GetStruct(dataA).Hash != GetStruct(dataB).Hash)
        return GetStringView(dataA) <=> GetStringView(dataB);
    return std::strong_ordering::equal;
}
template<typename T> std::string String<T>::GetDisplayText(Context const& context) const
{
    try
    {
        auto text = GetString(context);
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
template<typename T> void String<T>::Draw(Context const& context) const
{
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", GetDisplayText(context));
}

template<typename T> std::string StringPointer<T>::GetDisplayText(Context const& context) const
{
    if (auto const target = context.Data<typename String<T>::Struct const*>())
        return GetTargetSymbolType()->GetDisplayText({ target, context });
    return "";
}
template<typename T> ordered_json StringPointer<T>::Export(Context const& context, ExportOptions const& options) const
{
    if (auto const target = context.Data<typename String<T>::Struct const*>())
        return GetTargetSymbolType()->Export({ target, context }, options);
    return nullptr;
}
template<typename T> void StringPointer<T>::Draw(Context const& context) const
{
    if (auto const target = context.Data<typename String<T>::Struct const*>())
        GetTargetSymbolType()->Draw({ target, context });
}

template<std::array<byte, 4> Swizzle> uint32 Color<Swizzle>::GetRGBA(Context const& context)
{
    union
    {
        uint32 Color;
        byte Channels[4];
    } const original { .Color = context.Data<uint32>() };
    auto color = original;
    for (auto const& [dest, source] : Swizzle | std::views::enumerate)
        color.Channels[dest] = original.Channels[source];
    return color.Color;
}
template<std::array<byte, 4> Swizzle> std::string Color<Swizzle>::GetDisplayText(Context const& context) const
{
    return std::format("<c=#{0:08X}>#{0:08X}</c>", std::byteswap(GetRGBA(context)));
}
template<std::array<byte, 4> Swizzle> ordered_json Color<Swizzle>::Export(Context const& context, ExportOptions const& options) const
{
    return std::format("#{:08X}", std::byteswap(GetRGBA(context)));
}
template<std::array<byte, 4> Swizzle> void Color<Swizzle>::Draw(Context const& context) const
{
    ImVec4 const original = I::ColorConvertU32ToFloat4(context.Data<uint32>()); // 0xAABBGGRR
    ImVec4 color;
    for (auto const& [dest, source] : Swizzle | std::views::enumerate)
        ((float*)&color)[dest] = ((float const*)&original)[source];
    I::SetNextItemWidth(200);
    I::ColorEdit4("##Input", &color.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
}

static constexpr std::array pointAxes { "X", "Y", "Z", "W" };
static constexpr std::array pointColors { 0xFFCCCCFF, 0xFFCCFFCC, 0xFFFFCCCC, 0xFFFFCCFF };
template<typename T, size_t N> std::string Point<T, N>::GetDisplayText(Context const& context) const
{
    return std::format("({})", std::string { std::from_range, std::views::zip_transform([](T value, uint32 color) { return std::format("<c=#{:X}>{}</c>", std::byteswap(color), value); }, std::span { &context.Data<T>(), N }, pointColors) | std::views::join_with(std::string(", ")) });
}
template<typename T, size_t N> ordered_json Point<T, N>::Export(Context const& context, ExportOptions const& options) const
{
    ordered_json json;
    for (uint32 i = 0; i < N; ++i)
        json[pointAxes[i]] = (&context.Data<T>())[i];
    return json;
}
template<typename T, size_t N> void Point<T, N>::Draw(Context const& context) const
{
    for (auto const& [index, value, color] : std::views::zip(std::views::iota(0), std::span { &context.Data<T>(), N }, pointColors))
    {
        if (index)
            I::SameLine(0, 0);
        if (scoped::WithColorVar(ImGuiCol_Text, I::ColorConvertU32ToFloat4(color)))
            I::InputTextReadOnly(std::format("##Input{}", index).c_str(), std::format("{}", value));
    }

    I::SameLine(0, 0);
    if (I::Button(ICON_FA_GLOBE))
        ; // TOOD: Open world map to world-space coods
    if (auto const map = context.Content.GetMap(); map && map != &context.Content)
    {
        I::SameLine(0, 0);
        if (I::Button(std::format(ICON_FA_LOCATION_DOT " <c=#4>in</c> <c=#8>{}</c> {}", map->Type->GetDisplayName(), map->GetDisplayName()).c_str()))
            ; // TODO: Open world map to map-space coords
    }
}

std::string GUID::GetDisplayText(Context const& context) const
{
    if (auto const* object = *GetContent(context))
        return Utils::Encoding::ToUTF8(object->GetDisplayName(false, true));
    return std::format("{}", context.Data<GW2Viewer::GUID>());
}
std::optional<uint32> GUID::GetIcon(Context const& context) const
{
    if (auto const* object = *GetContent(context))
        return object->GetIcon();
    return { };
}
std::optional<ContentObject const*> GUID::GetMap(Context const& context) const
{
    if (auto const* object = *GetContent(context))
        return object->GetMap();
    return { };
}
std::optional<ContentObject const*> GUID::GetContent(Context const& context) const
{
    if (auto const object = G::Game.Content.GetByGUID(context.Data<GW2Viewer::GUID>()))
    {
        object->Finalize();
        return object;
    }
    return nullptr;
}
void GUID::Draw(Context const& context) const
{
    I::InputTextReadOnly("##Input", std::format("{}", context.Data<GW2Viewer::GUID>()));

    if (auto* object = *GetContent(context); object && object != &context.Content)
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
void Token32::Draw(Context const& context) const
{
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", GetDecoded(context).data());
}

std::strong_ordering Token64::CompareDataForSearch(byte const* dataA, byte const* dataB) const
{
    auto const a = GetDecoded(dataA);
    auto const b = GetDecoded(dataB);
    return std::string_view(a.data(), a.size() - 1) <=> std::string_view(b.data(), b.size() - 1);
}
void Token64::Draw(Context const& context) const
{
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", GetDecoded(context).data());
}

std::string StringID::GetDisplayText(Context const& context) const
{
    auto const stringID = GetStringID(context);
    auto [string, status] = G::Game.Text.Get(stringID);
    return std::format("{}{}", Encryption::GetStatusText(status), string ? *string : L"");
}
ordered_json StringID::Export(Context const& context, ExportOptions const& options) const
{
    if (auto const stringID = GetStringID(context))
    {
        if (auto const string = G::Game.Text.Get(stringID).first)
            return *string;

        return stringID;
    }
    return nullptr;
}
void StringID::Draw(Context const& context) const
{
    uint32 stringID = GetStringID(context);
    I::SetNextItemWidth(60);
    I::InputTextReadOnly("##InputID", std::format("{}", stringID));

    I::SameLine(0, 0);
    UI::Controls::TextVoiceButton(stringID);

    auto text = GetDisplayText(context);
    I::SameLine(0, 0);
    if (scoped::WithCursorOffset({ })) // Reserve space in tables
        I::ItemSize({ 100, 0 });
    if (context.Draw == TypeInfo::Symbol::DrawType::TableRow)
        I::SetNextItemWidth(-FLT_MIN);
    I::InputTextReadOnly("##Input", text, text.contains('\n') ? ImGuiInputTextFlags_Multiline : 0);
}

ordered_json FileID::Export(Context const& context, ExportOptions const& options) const
{
    if (auto const fileID = GetFileID(context))
        return fileID;
    return nullptr;
}
void FileID::Draw(Context const& context) const
{
    auto const fileID = GetFileID(context);
    I::SetNextItemWidth(70);
    I::InputTextReadOnly("##Input", std::format("{}", fileID));

    I::SameLine(0, 0);
    UI::Controls::FileButton(fileID);
}

void RawPointerT::Draw(Context const& context) const
{
    auto* ptr = *GetPointer(context);
    if (!ptr)
        I::PushStyleColor(ImGuiCol_Text, 0x40FFFFFF);

    if (!ptr || G::Config.ShowValidRawPointers)
        I::TextColored({ 0.75f, 0.75f, 1.0f, ptr ? 1.0f : 0.25f }, "0x%llX", ptr), I::SameLine();
    I::Text(ICON_FA_ARROW_RIGHT " %s", !context.Symbol.ElementTypeName.empty() ? context.Symbol.ElementTypeName.c_str() : "T");
    I::SameLine();
    I::SetNextItemWidth(120);
    I::DragCoerceInt("##ElementSize", (int*)&context.Symbol.ElementSize, 0.1f, 1, 10000, "Size: %u bytes", ImGuiSliderFlags_AlwaysClamp, [](auto v) { return std::max(1, (v / 4) * 4); });
    if (I::IsItemHovered())
        I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (!ptr)
        I::PopStyleColor();
}

std::string ContentPointer::GetDisplayText(Context const& context) const
{
    if (auto const* object = *GetContent(context))
        return Utils::Encoding::ToUTF8(object->GetDisplayName(false, true));
    return { };
}
std::optional<uint32> ContentPointer::GetIcon(Context const& context) const
{
    if (auto const* object = *GetContent(context))
        return object->GetIcon();
    return { };
}
std::optional<ContentObject const*> ContentPointer::GetMap(Context const& context) const
{
    if (auto const* object = *GetContent(context))
        return object->GetMap();
    return { };
}
std::optional<ContentObject const*> ContentPointer::GetContent(Context const& context) const
{
    if (auto const object = G::Game.Content.GetByDataPointer(*GetPointer(context)))
    {
        object->Finalize();
        return object;
    }
    return nullptr;
}
ordered_json ContentPointer::Export(Context const& context, ExportOptions const& options) const
{
    if (auto const object = *GetContent(context))
    {
        switch (options.ContentPointerFormat)
        {
            using enum ExportOptions::ContentPointerFormats;
            case GUID:
                return *object->GetGUID();
            case Verbose:
            {
                ordered_json json;
                json["Content::GUID"] = *object->GetGUID();
                json["Content::Type"] = Utils::Encoding::ToUTF8(object->Type->GetDisplayName());
                if (auto const dataID = object->GetDataID())
                    json["Content::DataID"] = *dataID;
                return json;
            }
            case Joined:
                if (auto const dataID = object->GetDataID())
                    return std::format("{1}{0}{2}{0}{3}", options.ContentPointerFormatJoinedSeparator, *object->GetGUID(), object->Type->GetDisplayName(), *dataID);
                return std::format("{1}{0}{2}", options.ContentPointerFormatJoinedSeparator, *object->GetGUID(), object->Type->GetDisplayName());
            default:
                std::terminate();
        }
    }
    return nullptr;
}
void ContentPointer::Draw(Context const& context) const
{
    auto* ptr = *GetPointer(context);
    auto* object = *GetContent(context);
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

void ArrayT::Draw(Context const& context) const
{
    auto* ptr = *GetPointer(context);
    if (!ptr)
        I::PushStyleColor(ImGuiCol_Text, 0x40FFFFFF);

    if (!ptr || G::Config.ShowValidRawPointers)
        I::TextColored({ 0.75f, 0.75f, 1.0f, ptr ? 1.0f : 0.25f }, "0x%llX", ptr), I::SameLine();
    I::Text(ICON_FA_ARROW_RIGHT " %s[%u]", !context.Symbol.ElementTypeName.empty() ? context.Symbol.ElementTypeName.c_str() : "T", *GetArrayCount(context));
    I::SameLine();
    I::SetNextItemWidth(120);
    I::DragCoerceInt("##ElementSize", (int*)&context.Symbol.ElementSize, 0.1f, 1, 10000, "Element: %u bytes", ImGuiSliderFlags_AlwaysClamp, [](auto v) { return std::max(1, (v / 4) * 4); });
    if (I::IsItemHovered())
        I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    I::SameLine();
    I::Text("Total: %u bytes", *GetArrayCount(context) * context.Symbol.ElementSize);

    if (!ptr)
        I::PopStyleColor();
}

std::optional<ContentObject const*> ArrayContent::GetContent(Context const& context) const
{
    if (auto const object = G::Game.Content.GetByDataPointer(*GetPointer(context)))
    {
        object->Finalize();
        return object;
    }
    return nullptr;
}
void ArrayContent::Draw(Context const& context) const
{
    auto* ptr = *GetPointer(context);
    auto const* object = *GetContent(context);
    if (!ptr)
        I::PushStyleColor(ImGuiCol_Text, 0x40FFFFFF);
    else if (!object)
        I::PushStyleColor(ImGuiCol_Text, 0xFF0000FF);

    if (ptr && !object)
        I::Text("0x%llX", ptr), I::SameLine();
    else if (!ptr || G::Config.ShowValidRawPointers)
        I::TextColored({ 0.75f, 0.75f, 1.0f, ptr ? 1.0f : 0.25f }, "0x%llX", ptr), I::SameLine();

    I::Text(ICON_FA_ARROW_RIGHT " %s[%u]", object ? Utils::Encoding::ToUTF8(object->Type->GetDisplayName()).c_str() : "Content", *GetArrayCount(context));
    I::SameLine();
    I::SetNextItemWidth(120);
    I::DragCoerceInt("##ElementSize", (int*)&context.Symbol.ElementSize, 0.1f, 1, 10000, "Element: %u bytes", ImGuiSliderFlags_AlwaysClamp, [](auto v) { return std::max(1, (v / 4) * 4); });
    if (I::IsItemHovered())
        I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    I::SameLine();
    I::Text("Total: %u bytes", *GetArrayCount(context) * context.Symbol.ElementSize);

    if (!ptr || !object)
        I::PopStyleColor();
}

std::string ContentType::GetDisplayText(Context const& context) const
{
    auto const typeIndex = context.Data<uint32>();
    if (auto const& typeInfo = G::Game.Content.GetType(typeIndex)->GetTypeInfo(); !typeInfo.Name.empty())
        return std::format("{}  <c=#4>#{}</c>", typeInfo.Name, typeIndex);

    return std::format("<c=#4>#{}</c>", typeIndex);
}
ordered_json ContentType::Export(Context const& context, ExportOptions const& options) const
{
    auto const typeIndex = context.Data<uint32>();
    if (auto const& typeInfo = G::Game.Content.GetType(typeIndex)->GetTypeInfo(); !typeInfo.Name.empty())
        return typeInfo.Name;

    return typeIndex;
}

std::tuple<TypeInfo::SymbolType const*, uint32> GetSymbolTypeForContentType(uint32 typeIndex)
{
    switch (G::Game.Content.GetType(typeIndex)->GetTypeInfo().ContentType)
    {
        using enum GW2Viewer::Content::EContentTypes;
        case Boolean:       return { GetByName("bool"),     1 };
        case Enum:          return { GetByName("int8"),     1 };
        case Flags:         return { GetByName("int16"),    1 };
        case Time:
        case TimeOfDay: // ???
        case Integer:       return { GetByName("int32"),    1 };
        case IntegerPair:
        case IntegerRange:  return { GetByName("int32"),    2 };
        case Number:        return { GetByName("float"),    1 };
        case NumberPair:
        case NumberRange:   return { GetByName("float"),    2 };
        case Point3d:       return { GetByName("float"),    3 };
        case Variable:
        case String:        return { GetByName("wchar_t*"), 1 };
        case Token32:       return { GetByName("Token32"),  1 };
        case Token64:       return { GetByName("Token64"),  1 };
        case TextCoded: // ???
        case Text:          return { GetByName("StringID"), 1 };
        case None:
        default:            return { GetByName("Content*"), 1 };
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
std::optional<TypeInfo::Condition::ValueType> ParamValue::GetValueForCondition(Context const& context) const
{
    auto const& param = GetStruct(context);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
        return typeInfo->GetValueForCondition({ param.Raw, context });
    return { };
}
std::string ParamValue::GetDisplayText(Context const& context) const
{
    auto const& param = GetStruct(context);
    if (param.ContentType >= G::Game.Content.GetNumTypes())
        return std::format("<c=#F00>{}</c>", (uint32)param.ContentType);

    auto const typeName = std::format("<c=#8>{}</c> {}", ICON_FA_GEAR, G::Game.Content.GetType((uint32)param.ContentType)->GetDisplayName());
    switch (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); count)
    {
        default: return std::format("<c=#4>{}</c> {}",           typeName, typeInfo ? typeInfo->GetDisplayText({ param.Raw, context }) : "<c=#F00>Unhandled basic type</c>");
        case 2:  return std::format("<c=#4>{}</c> ({}, {})",     typeName, typeInfo->GetDisplayText({ param.Raw, context }), typeInfo->GetDisplayText({ &param.Raw[typeInfo->Size()], context }));
        case 3:  return std::format("<c=#4>{}</c> ({}, {}, {})", typeName, typeInfo->GetDisplayText({ param.Raw, context }), typeInfo->GetDisplayText({ &param.Raw[typeInfo->Size()], context }), typeInfo->GetDisplayText({ &param.Raw[typeInfo->Size() * 2], context }));
    }
}
std::optional<uint32> ParamValue::GetIcon(Context const& context) const
{
    auto const& param = GetStruct(context);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
        return typeInfo->GetIcon({ param.Raw, context });
    return { };
}
std::optional<ContentObject const*> ParamValue::GetMap(Context const& context) const
{
    auto const& param = GetStruct(context);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
        return typeInfo->GetMap({ param.Raw, context });
    return { };
}
std::optional<byte const*> ParamValue::GetPointer(Context const& context) const
{
    auto const& param = GetStruct(context);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
        return typeInfo->GetPointer({ param.Raw, context });
    return { };
}
std::optional<ContentObject const*> ParamValue::GetContent(Context const& context) const
{
    auto const& param = GetStruct(context);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
        return typeInfo->GetContent({ param.Raw, context });
    return { };
}
ordered_json ParamValue::Export(Context const& context, ExportOptions const& options) const
{
    auto const& param = GetStruct(context);
    if (auto [typeInfo, count] = GetSymbolTypeForContentType(param.ContentType); typeInfo)
    {
        if (count <= 1)
            return typeInfo->Export({ param.Raw, context }, options);

        auto array = ordered_json::array();
        for (uint32 i = 0; i < count; ++i)
            array.emplace_back(typeInfo->Export({ &param.Raw[i * typeInfo->Size()], context }, options));
        return array;
    }
    return "Unhandled basic type";
}
void ParamValue::Draw(Context const& context) const
{
    auto const& param = GetStruct(context);
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
            typeInfo->Draw({ p, context });
            p += typeInfo->Size();
        }
    }
    else
    {
        I::SameLine();
        I::Text("<c=#F00>Unhandled basic type</c>");
    }
    if (param.GUID != GW2Viewer::GUID::Empty)
        GetByName("GUID")->Draw({ &param.GUID, context });
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
std::optional<TypeInfo::Condition::ValueType> ParamDeclare::GetValueForCondition(Context const& context) const
{
    auto const& param = GetStruct(context);
    return GetByName("ParamValue")->GetValueForCondition({ &param.Value, context });
}
std::optional<uint32> ParamDeclare::GetIcon(Context const& context) const
{
    auto const& param = GetStruct(context);
    return GetByName("ParamValue")->GetIcon({ &param.Value, context });
}
std::optional<ContentObject const*> ParamDeclare::GetMap(Context const& context) const
{
    auto const& param = GetStruct(context);
    return GetByName("ParamValue")->GetMap({ &param.Value, context });
}
std::optional<byte const*> ParamDeclare::GetPointer(Context const& context) const
{
    auto const& param = GetStruct(context);
    return GetByName("ParamValue")->GetPointer({ &param.Value, context });
}
std::optional<ContentObject const*> ParamDeclare::GetContent(Context const& context) const
{
    auto const& param = GetStruct(context);
    return GetByName("ParamValue")->GetContent({ &param.Value, context });
}
ordered_json ParamDeclare::Export(Context const& context, ExportOptions const& options) const
{
    auto const& param = GetStruct(context);
    return GetByName("ParamValue")->Export({ &param.Value, context }, options);
}
void ParamDeclare::Draw(Context const& context) const
{
    auto const& param = GetStruct(context);
    if (scoped::Group())
    {
        I::InputTextReadOnly("##NameInput", GetByName("wchar_t*")->GetDisplayText({ &param.Name, context }));
        I::SameLine();
        GetByName("ParamValue")->Draw({ &param.Value, context });
    }
}

std::string MetaContentName::GetDisplayText(Context const& context) const
{
    return Utils::Encoding::ToUTF8(context.Data<ContentObject>().GetDisplayName(false, false, true));
}
std::string MetaContentPath::GetDisplayText(Context const& context) const
{
    auto const& content = context.Data<ContentObject>();
    return Utils::Encoding::ToUTF8(content.Root ? content.Root->GetFullDisplayName(false, false, true) : content.Namespace->GetFullDisplayName());
}
std::string MetaContentType::GetDisplayText(Context const& context) const
{
    return Utils::Encoding::ToUTF8(context.Data<ContentObject>().Type->GetDisplayName());
}
std::string MetaContentIcon::GetDisplayText(Context const& context) const
{
    auto const icon = GetIcon(context);
    return icon ? std::format("<img={}/>", icon) : "";
}
std::optional<uint32> MetaContentIcon::GetIcon(Context const& context) const
{
    return context.Data<ContentObject>().GetIcon();
}
std::string MetaContentMap::GetDisplayText(Context const& context) const
{
    auto const map = context.Data<ContentObject>().GetMap();
    return map ? Utils::Encoding::ToUTF8(map->GetDisplayName(false, false, true)) : "";
}
std::optional<ContentObject const*> MetaContentMap::GetMap(Context const& context) const
{
    return context.Data<ContentObject>().GetMap();
}
std::string MetaContentDisplay::GetDisplayText(Context const& context) const
{
    return Utils::Encoding::ToUTF8(context.Data<ContentObject>().GetDisplayName());
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
        new Color<{ 0, 1, 2, 3 }>("ColorRGBA"),
        new Color<{ 2, 1, 0, 3 }>("ColorBGRA"),
        new Color<{ 3, 0, 1, 2 }>("ColorARGB"),
        new Color<{ 3, 2, 1, 0 }>("ColorABGR"),
        new Point<float, 2>("Point2D"),
        new Point<float, 3>("Point3D"),
        new Point<int, 2>("IntPoint2D"),
        new Point<int, 3>("IntPoint3D"),
        new GUID(),
        new Token32(),
        new Token64(),
        new StringID(),
        new FileID(),
        new ContentType(),
        new ContentPointer(),
        new ArrayContent(),
        new RawPointerT(),
        new ArrayT(),
        new ParamValue(),
        new ParamDeclare(),

        new MetaContentName(),
        new MetaContentPath(),
        new MetaContentType(),
        new MetaContentIcon(),
        new MetaContentMap(),
        new MetaContentDisplay(),
        new MetaContentIconName(),
        new MetaContentIconDisplay(),
        new MetaContentSelf(), // Must go last
    };
    return instance;
}

}
