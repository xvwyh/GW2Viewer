module;
#include "UI/ImGui/ImGui.h"
#include "imgui_impl_dx11.h"

export module GW2Viewer.UI.Viewers.PackFileViewer;
import GW2Viewer.Common;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Pack;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.Utils.Encoding;
import std;

namespace GW2Viewer
{

std::string* g_writeTokensTargets;
std::string* g_writeStringsTargets;

template<template<typename PointerType> typename PackFileType, typename PointerType> struct DrawPackFileField { };
template<template<typename SizeType, typename PointerType> typename PackFileType, typename SizeType, typename PointerType> struct DrawPackFileFieldArray { };

void DrawPackFileType(byte const*& p, bool x64, Data::Pack::Layout::Type const* type, Data::Pack::Layout::Field const* parentField = nullptr);
template<template<typename PointerType> typename PackFileType>
void DrawPackFileFieldByArch(byte const*& p, Data::Pack::Layout::Field const& field, bool x64)
{
    if (x64)
        DrawPackFileField<PackFileType, int64> { }((PackFileType<int64> const*&)p, field);
    else
        DrawPackFileField<PackFileType, int32> { }((PackFileType<int32> const*&)p, field);
}
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericPtr, PointerType>
{
    void operator()(Data::Pack::GenericPtr<PointerType> const*& p, Data::Pack::Layout::Field const& field)
    {
        byte const* ep = p->get();
        ++p;

        I::SameLine();
        I::Text("<c=#4>%s*</c>", field.ElementType->Name.c_str());
        if (!ep)
        {
            I::SameLine();
            I::TextUnformatted("<c=#CCF><nullptr></c>");
            return;
        }
        I::Dummy({ 25, 0 });
        I::SameLine();
        if (scoped::Group())
            DrawPackFileType(ep, std::is_same_v<PointerType, int64>, field.ElementType, &field);
    }
};
template<typename SizeType, typename PointerType> struct DrawPackFileFieldArray<Data::Pack::GenericArray, SizeType, PointerType>
{
    void operator()(Data::Pack::GenericArray<SizeType, PointerType> const*& p, Data::Pack::Layout::Field const& field)
    {
        byte const* ep = p->data();
        uint32 const size = p->size();
        ++p;

        I::SameLine();
        I::Text("<c=#4>%s[%u]</c>", field.ElementType->Name.c_str(), size);
        if (size > 10 && (field.ElementType->Name == "byte" || field.ElementType->Name == "word" || field.ElementType->Name == "float" || field.ElementType->Name == "float3"))
        {
            I::SameLine();
            I::Text("<c=#0F0><omitted></c>");
            return;
        }
        I::Dummy({ 25, 0 });
        I::SameLine();
        if (scoped::Group())
        for (uint32 i = 0; i < size; ++i)
        {
            I::Text("[%u] ", i);
            I::SameLine();
            if (scoped::Group())
                DrawPackFileType(ep, std::is_same_v<PointerType, int64>, field.ElementType, &field);
        }
    }
};
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericDwordArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericArray, uint32, PointerType> { };
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericWordArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericArray, uint16, PointerType> { };
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericByteArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericArray, byte, PointerType> { };
template<typename SizeType, typename PointerType> struct DrawPackFileFieldArray<Data::Pack::GenericPtrArray, SizeType, PointerType>
{
    void operator()(Data::Pack::GenericPtrArray<SizeType, PointerType> const*& p, Data::Pack::Layout::Field const& field)
    {
        Data::Pack::GenericPtr<PointerType> const* ep = p->data();
        uint32 const size = p->size();
        ++p;

        I::SameLine();
        I::Text("<c=#4>%s*[%u]</c>", field.ElementType->Name.c_str(), size);
        I::Dummy({ 25, 0 });
        I::SameLine();
        if (scoped::Group())
        for (uint32 i = 0; i < size; ++i)
        {
            I::Text("[%u] ", i);
            I::SameLine();
            if (scoped::Group())
                DrawPackFileField<Data::Pack::GenericPtr, PointerType> { }(ep, field);
        }
    }
};
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericDwordPtrArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericPtrArray, uint32, PointerType> { };
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericWordPtrArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericPtrArray, uint16, PointerType> { };
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericBytePtrArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericPtrArray, byte, PointerType> { };
template<typename SizeType, typename PointerType> struct DrawPackFileFieldArray<Data::Pack::GenericTypedArray, SizeType, PointerType>
{
    void operator()(Data::Pack::GenericTypedArray<SizeType, PointerType> const*& p, Data::Pack::Layout::Field const& field)
    {
        byte const* ep = p->data();
        uint32 const size = p->size();
        ++p;

        std::terminate(); // TODO: Not yet implemented
    }
};
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericDwordTypedArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericTypedArray, uint32, PointerType> { };
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericWordTypedArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericTypedArray, uint16, PointerType> { };
template<typename PointerType> struct DrawPackFileField<Data::Pack::GenericByteTypedArray, PointerType> : DrawPackFileFieldArray<Data::Pack::GenericTypedArray, byte, PointerType> { };
template<typename PointerType> struct DrawPackFileField<Data::Pack::FileNameBase, PointerType>
{
    void operator()(Data::Pack::FileNameBase<PointerType> const*& p, Data::Pack::Layout::Field const& field)
    {
        auto const fileID = p->GetFileID();
        scoped::WithID(p);
        ++p;

        I::SameLine();
        I::Button(std::format("File <{}>", fileID).c_str());
        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
            if (auto* file = G::Game.Archive.GetFileEntry(fileID))
                UI::Viewers::FileViewer::Open(*file, { .MouseButton = button });
        if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
            UI::Controls::Texture(fileID);
    }
};
template<typename PointerType> struct DrawPackFileField<Data::Pack::String, PointerType>
{
    void operator()(Data::Pack::String<PointerType> const*& p, Data::Pack::Layout::Field const& field)
    {
        I::SameLine();
        if (auto const* string = p->data())
        {
            I::TextUnformatted(string);
            if (g_writeStringsTargets)
                *g_writeStringsTargets += std::format("{}\n", string);
        }
        else
            I::TextUnformatted("<c=#CCF><nullptr></c>");
        ++p;
    }
};
template<typename PointerType> struct DrawPackFileField<Data::Pack::WString, PointerType>
{
    void operator()(Data::Pack::WString<PointerType> const*& p, Data::Pack::Layout::Field const& field)
    {
        I::SameLine();
        if (auto const* string = p->data())
        {
            I::TextUnformatted(Utils::Encoding::ToUTF8(string).c_str());
            if (g_writeStringsTargets)
                *g_writeStringsTargets += std::format("{}\n", Utils::Encoding::ToUTF8(string));
        }
        else
            I::TextUnformatted("<c=#CCF><nullptr></c>");
        ++p;
    }
};
template<typename PointerType> struct DrawPackFileField<Data::Pack::Variant, PointerType>
{
    void operator()(Data::Pack::Variant<PointerType> const*& p, Data::Pack::Layout::Field const& field)
    {
        byte const* ep = p->data();
        auto const& type = field.VariantElementTypes.at(p->index());
        ++p;

        I::SameLine();
        I::TextUnformatted(std::format("<c=#4>Variant<{}></c>", std::string { std::from_range, field.VariantElementTypes | std::views::transform([](Data::Pack::Layout::Type const* type) { return type->Name; }) | std::views::join_with(',') }).c_str());
        I::Dummy({ 25, 0 });
        I::SameLine();
        I::Text("[%s] ", type->Name.c_str());
        I::SameLine();
        if (scoped::Group())
            DrawPackFileType(ep, std::is_same_v<PointerType, int64>, type, &field);
    }
};
void DrawPackFileFieldValue(byte const*& p, bool x64, Data::Pack::Layout::Field const& field, Data::Pack::Layout::Field const* parentField = nullptr)
{
    // TODO: field.RealType
    switch (field.UnderlyingType)
    {
        using enum Data::Pack::Layout::UnderlyingTypes;
        case Byte:
            I::SameLine();
            I::Text("<c=#4>byte</c> %u", (uint32)*p++);
            break;
        case Byte3:
            I::SameLine();
            I::Text("<c=#4>byte3</c> (%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u)", (uint32)*p++);
            break;
        case Byte4:
            I::SameLine();
            I::Text("<c=#4>byte4</c> (%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u, ", (uint32)*p++);
            I::SameLine(0, 0);
            I::Text("%u)", (uint32)*p++);
            break;
        case Byte16:
            I::SameLine();
            I::Text("<c=#4>byte16</c> (%u, ", (uint32)*p++);
            for (uint32 i = 0; i < 14; ++i)
            {
                I::SameLine(0, 0);
                I::Text("%u, ", (uint32)*p++);
            }
            I::SameLine(0, 0);
            I::Text("%u)", (uint32)*p++);
            break;
        case Word:
            I::SameLine();
            I::Text("<c=#4>word</c> %d", (int32)*((int16 const*&)p)++);
            break;
        case Word3:
            I::SameLine();
            I::Text("<c=#4>word3</c> (%d, ", (int32)*((int16 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d, ", (int32)*((int16 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d)", (int32)*((int16 const*&)p)++);
            break;
        case Dword:
        case DwordID:
            I::SameLine();
            if (field.RealType == Data::Pack::Layout::RealTypes::Token || parentField && parentField->RealType == Data::Pack::Layout::RealTypes::Token)
            {
                auto token = ((Token32 const*)p)->GetString();
                I::Text("<c=#4>token32</c> %s", token.data());
                if (g_writeTokensTargets && *token.data())
                    *g_writeTokensTargets += std::format("{}\n", token.data());
            }
            else
            {
                I::Text("<c=#4>dword</c> %d", *(int32 const*)p);
                I::SameLine(0, 0);
                auto token = ((Token32 const*)p)->GetString();
                I::Text("<c=#4> or token32</c> %s", token.data());
                if (g_writeTokensTargets && *token.data())
                    *g_writeTokensTargets += std::format("{}\n", token.data());
            }
            ++(int32 const*&)p;
            break;
        case Dword2:
            I::SameLine();
            I::Text("<c=#4>dword2</c> (%d, ", *((int32 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d)", *((int32 const*&)p)++);
            break;
        case Dword4:
            I::SameLine();
            I::Text("<c=#4>dword4</c> (%d, ", *((int32 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d, ", *((int32 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d, ", *((int32 const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%d)", *((int32 const*&)p)++);
            break;
        case Qword:
        case QwordID:
            I::SameLine();
            if (field.RealType == Data::Pack::Layout::RealTypes::Token || parentField && parentField->RealType == Data::Pack::Layout::RealTypes::Token)
            {
                auto token = ((Token64 const*)p)->GetString();
                I::Text("<c=#4>token64</c> %s", token.data());
                if (g_writeTokensTargets && *token.data())
                    *g_writeTokensTargets += std::format("{}\n", token.data());
            }
            else
            {
                I::TextUnformatted(std::format("<c=#4>qword</c> {}", *(int64 const*)p).c_str());
                I::SameLine(0, 0);
                auto token = ((Token64 const*)p)->GetString();
                I::Text("<c=#4> or token64</c> %s", token.data());
                if (g_writeTokensTargets && *token.data())
                    *g_writeTokensTargets += std::format("{}\n", token.data());
            }
            ++(int64 const*&)p;
            break;
        case Float:
            I::SameLine();
            I::Text("<c=#4>float</c> %f", *((float const*&)p)++);
            break;
        case Float2:
            I::SameLine();
            I::Text("<c=#4>float2</c> (%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((float const*&)p)++);
            break;
        case Float3:
            I::SameLine();
            I::Text("<c=#4>float3</c> (%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((float const*&)p)++);
            break;
        case Float4:
            I::SameLine();
            I::Text("<c=#4>float4</c> (%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f, ", *((float const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((float const*&)p)++);
            break;
        case Double:
            I::SameLine();
            I::Text("<c=#4>double</c> %f", *((double const*&)p)++);
            break;
        case Double2:
            I::SameLine();
            I::Text("<c=#4>double2</c> (%f, ", *((double const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((double const*&)p)++);
            break;
        case Double3:
            I::SameLine();
            I::Text("<c=#4>double3</c> (%f, ", *((double const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f, ", *((double const*&)p)++);
            I::SameLine(0, 0);
            I::Text("%f)", *((double const*&)p)++);
            break;
        case InlineArray:
            I::SameLine();
            I::Text("<c=#4>%s[%u]</c>", field.ElementType->Name.c_str(), field.ArraySize);
            I::Dummy({ 25, 0 });
            I::SameLine();
            if (scoped::Group())
            for (uint32 i = 0; i < field.ArraySize; ++i)
            {
                I::Text("[%u] ", i);
                I::SameLine();
                if (scoped::Group())
                    DrawPackFileType(p, x64, field.ElementType, &field);
            }
            break;
        case DwordArray: DrawPackFileFieldByArch<Data::Pack::GenericDwordArray>(p, field, x64); break;
        case WordArray: DrawPackFileFieldByArch<Data::Pack::GenericWordArray>(p, field, x64); break;
        case ByteArray: DrawPackFileFieldByArch<Data::Pack::GenericByteArray>(p, field, x64); break;
        case DwordPtrArray: DrawPackFileFieldByArch<Data::Pack::GenericDwordPtrArray>(p, field, x64); break;
        case WordPtrArray: DrawPackFileFieldByArch<Data::Pack::GenericWordPtrArray>(p, field, x64); break;
        case BytePtrArray: DrawPackFileFieldByArch<Data::Pack::GenericBytePtrArray>(p, field, x64); break;
        case DwordTypedArray: DrawPackFileFieldByArch<Data::Pack::GenericDwordTypedArray>(p, field, x64); break;
        case WordTypedArray: DrawPackFileFieldByArch<Data::Pack::GenericWordTypedArray>(p, field, x64); break;
        case ByteTypedArray: DrawPackFileFieldByArch<Data::Pack::GenericByteTypedArray>(p, field, x64); break;
        case FileName:
        case FileName2: DrawPackFileFieldByArch<Data::Pack::FileNameBase>(p, field, x64); break;
        case Ptr: DrawPackFileFieldByArch<Data::Pack::GenericPtr>(p, field, x64); break;
        case String: DrawPackFileFieldByArch<Data::Pack::String>(p, field, x64); break;
        case WString: DrawPackFileFieldByArch<Data::Pack::WString>(p, field, x64); break;
        case Variant: DrawPackFileFieldByArch<Data::Pack::Variant>(p, field, x64); break;
        case InlineStruct:
        case InlineStruct2:
            I::Dummy({ 25, 0 });
            I::SameLine();
            if (scoped::Group())
                DrawPackFileType(p, x64, field.ElementType, &field);
            break;
        default:
            I::SameLine();
            I::Text("<c=#F00>Unhandled type %u</c>", (uint32)field.UnderlyingType);
            break;
    }
}
void DrawPackFileType(byte const*& p, bool x64, Data::Pack::Layout::Type const* type, Data::Pack::Layout::Field const* parentField)
{
    for (auto const& field : type->Fields)
    {
        I::Text("<c=#8>%s = </c>", field.Name.c_str());
        DrawPackFileFieldValue(p, x64, field, parentField);
    }
}

struct PackFileChunkPreviewBase
{
    virtual ~PackFileChunkPreviewBase() = default;
    virtual void DrawPreview(Data::Pack::Layout::Traversal::QueryChunk const& chunk) { }
};
auto& GetPackFileChunkPreviewRegistry()
{
    static std::unordered_map<fcc, std::function<PackFileChunkPreviewBase*()>> instance { };
    return instance;
}
template<fcc FourCC>
struct RegisterPackFileChunkPreview
{
    static bool Register();
    inline static bool Registered = Register();
};

template<fcc FourCC>
struct PackFileChunkPreview : PackFileChunkPreviewBase { };

template<>
struct PackFileChunkPreview<fcc::PGTB> : RegisterPackFileChunkPreview<fcc::PGTB>, PackFileChunkPreviewBase
{
    std::optional<ImVec2> ViewportOffset { };

    void DrawPreview(Data::Pack::Layout::Traversal::QueryChunk const& chunk) override
    {
        struct Layer
        {
            ImVec2 StrippedDims { };
            ImRect ContentsRect { };
        };
        std::vector<Layer> layers;
        layers.reserve(chunk["layers"].GetArraySize());
        for (auto const& layerData : chunk["layers"])
        {
            auto& layer = layers.emplace_back();
            std::array<float, 2> strippedDims = layerData["strippedDims"];
            layer.StrippedDims = { strippedDims[0], strippedDims[1] };
        }

#pragma pack(push, 4)
        static struct PixelBuffer
        {
            ImVec4 Channels { 1, 1, 1, 1 };
            int AlphaMode;
            float Padding[3];
        } pixelBuffer { };
#pragma pack(pop)
        static_assert(sizeof(PixelBuffer) % 16 == 0); // Shader requirement to buffers
        if (bool channel = pixelBuffer.Channels.x == 1; I::CheckboxButton("<c=#F00>" ICON_FA_SQUARE "</c>", channel, "Show Red Channel", { I::GetFrameHeight(), I::GetFrameHeight() }))
            pixelBuffer.Channels.x = channel ? 1 : 0;
        I::SameLine(0, 0);
        if (bool channel = pixelBuffer.Channels.y == 1; I::CheckboxButton("<c=#0F0>" ICON_FA_SQUARE "</c>", channel, "Show Green Channel", { I::GetFrameHeight(), I::GetFrameHeight() }))
            pixelBuffer.Channels.y = channel ? 1 : 0;
        I::SameLine(0, 0);
        if (bool channel = pixelBuffer.Channels.z == 1; I::CheckboxButton("<c=#00F>" ICON_FA_SQUARE "</c>", channel, "Show Blue Channel", { I::GetFrameHeight(), I::GetFrameHeight() }))
            pixelBuffer.Channels.z = channel ? 1 : 0;
        I::SameLine();
        I::AlignTextToFramePadding();
        I::TextUnformatted(ICON_FA_GAME_BOARD_SIMPLE);
        I::SameLine(0, 0);
        if (I::Button(std::format("<c=#{}>" ICON_FA_VIRUS "</c><c=#{}>" ICON_FA_SQUARE_VIRUS "</c><c=#{}>" ICON_FA_SQUARE "</c>###AlphaMode", pixelBuffer.AlphaMode == 0 ? "F" : "4", pixelBuffer.AlphaMode == 1 ? "F" : "4",             pixelBuffer.AlphaMode == 2 ? "F" : "4").c_str(), { 0, I::GetFrameHeight() }))
            pixelBuffer.AlphaMode = (pixelBuffer.AlphaMode + 1) % 3;
        static int selectedLayer = -1;
        if (layers.size() > 1)
        {
            I::SameLine();
            I::TextUnformatted("Layer:");
            I::SameLine();
            I::SetNextItemWidth(100);
            I::SliderInt("##Layer", &selectedLayer, -1, layers.size() - 1);
        }
        if (selectedLayer >= (int)layers.size())
            selectedLayer = layers.size();
        static auto buffer = ImGui_ImplDX11_CreateBuffer(sizeof(PixelBuffer));
        static auto shader = ImGui_ImplDX11_CompilePixelShader(R"(
cbuffer pixelBuffer : register(b0)
{
    float4 Channels;
    int AlphaMode;
    float Padding[2];
};
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};
sampler sampler0;
Texture2D texture0;

float4 main(PS_INPUT input) : SV_Target
{
    float4 tex = texture0.Sample(sampler0, input.uv);
    float4 out_col = input.col * tex * Channels;
    if (AlphaMode == 1)
        out_col.rgba = float4(out_col.aaa, 1.0f);
    else if (AlphaMode == 2)
        out_col.a = 1.0f;
    return out_col;
}
)");

        auto const cursor = I::GetCursorScreenPos();
        auto const viewportSize = I::GetContentRegionAvail();
        ImRect const viewportScreenRect { cursor, cursor + viewportSize };
        scoped::Child("Viewport", viewportSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        I::InvisibleButton("Canvas", viewportSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

        bool const initViewportOffset = !ViewportOffset;
        if (initViewportOffset)
            ViewportOffset.emplace();
        if (I::IsItemActive() && I::IsMouseDragging(ImGuiMouseButton_Left))
        {
            float const scale = (I::GetIO().KeyShift ? 10.0f : 1.0f) * (I::GetIO().KeyCtrl ? 100.0f : 1.0f);
            *ViewportOffset -= I::GetMouseDragDelta(ImGuiMouseButton_Left) * scale;
            I::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
        *ViewportOffset = { (float)(int)ViewportOffset->x, (float)(int)ViewportOffset->y };

        I::GetWindowDrawList()->AddCallback([](ImDrawList const* parent_list, ImDrawCmd const* cmd)
        {
            ImGui_ImplDX11_SetPixelShader(shader);
            ImGui_ImplDX11_SetPixelShaderConstantBuffer(buffer, &pixelBuffer, sizeof(pixelBuffer));
        }, nullptr);

        for (auto const& pageData : chunk["strippedPages"])
        {
            std::array<float, 2> const coord = pageData["coord"];
            uint32 const layerIndex = pageData["layer"];

            auto& layer = layers.at(layerIndex);
            ImVec2 pagePos = layer.StrippedDims * ImVec2 { coord[0], coord[1] };
            if (layer.ContentsRect.Min == layer.ContentsRect.Max)
                layer.ContentsRect = { pagePos, pagePos + layer.StrippedDims };
            else
                layer.ContentsRect.Add(ImRect { pagePos, pagePos + layer.StrippedDims });
            ImVec2 drawPos = cursor - *ViewportOffset + pagePos;
            if ((layerIndex == selectedLayer || selectedLayer < 0) && viewportScreenRect.Overlaps({ drawPos, drawPos + layer.StrippedDims }))
                if (scoped::WithCursorScreenPos(drawPos))
                    UI::Controls::Texture(pageData["filename"], { .Size = layer.StrippedDims, .FullPreviewOnHover = false, .AdvanceCursor = false });
        }

        I::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

        if (initViewportOffset)
            ViewportOffset = layers[0].ContentsRect.GetCenter() - viewportSize * 0.5f;
    }
};

template<fcc FourCC> bool RegisterPackFileChunkPreview<FourCC>::Register()
{
    return [] { return GetPackFileChunkPreviewRegistry().emplace(FourCC, []<typename... Args>(Args&&... args) { return new PackFileChunkPreview<FourCC>(std::forward<Args>(args)...); }).second; }();
}

}

export namespace GW2Viewer::UI::Viewers
{

struct PackFileViewer : FileViewer
{
    using FileViewer::FileViewer;

    std::unique_ptr<Data::Pack::PackFile> PackFile;
    std::vector<std::unique_ptr<PackFileChunkPreviewBase>> ChunkPreview;

    void Initialize() override
    {
        PackFile = File.Source.get().Archive.GetPackFile(File.ID);
    }
    void DrawOutline() override
    {
        g_writeTokensTargets = nullptr;
        if (static ImGuiID sharedScope = 3; scoped::Child(sharedScope, { }, ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
        {
            if (scoped::TabBar("Tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_NoTabListScrollingButtons))
            {
                if (scoped::TabItem(ICON_FA_MAGNIFYING_GLASS " Search", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
                {
                    static bool resultsAsTree = false;
                    I::CheckboxButton(ICON_FA_FOLDER_TREE, resultsAsTree, "Expand Results into Trees", I::GetFrameHeight());
                    I::SameLine();
                    static std::string query;
                    I::TextUnformatted("Query:");
                    I::SameLine();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::InputText("##Query", &query);
                    if (!query.empty())
                    {
                        try
                        {
                            if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2(I::GetStyle().CellPadding.x, 0)))
                            if (scoped::Table("Results", 3))
                            {
                                I::TableSetupColumn("Chunk", ImGuiTableColumnFlags_WidthFixed);
                                I::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed);
                                I::TableSetupColumn(std::format("{}###Value", query).c_str());
                                I::TableHeadersRow();

                                uint32 i = 0;
                                for (auto const& chunk : *PackFile)
                                {
                                    std::string const fcc { (char const*)&chunk.Header.Magic, 4 };
                                    for (auto const& field : Data::Pack::Layout::Traversal::QueryFields(*PackFile, chunk, query))
                                    {
                                        auto p = field.GetPointer();
                                        I::TableNextRow();
                                        I::TableNextColumn(); I::Text("<c=#4>%s</c>", fcc.c_str());
                                        I::TableNextColumn(); I::Text("<c=#4>#</c><c=#8>%u</c>", i++);
                                        I::TableNextColumn();
                                        if (field.IsArrayIterator())
                                        {
                                            if (resultsAsTree)
                                                DrawPackFileType(p, PackFile->Header.Is64Bit, &field.GetArrayType());
                                            else
                                                DrawPackFileFieldValue(p, PackFile->Header.Is64Bit, field.GetField().ElementType->Fields.front());
                                        }
                                        else
                                            DrawPackFileFieldValue(p, PackFile->Header.Is64Bit, field.GetField());
                                    }
                                }
                            }
                        }
                        catch (std::exception const& ex)
                        {
                            I::Text("<c=#F00>Error: %s</c>", ex.what());
                        }
                        I::Dummy({ 0, 20 });
                    }
                }
                if (scoped::TabItem(ICON_FA_TEXT_SIZE " Tokens", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                {
                    static std::string tokens;
                    if (I::Button(ICON_FA_COPY " Copy All"))
                        I::SetClipboardText(tokens.c_str());
                    if (tokens.empty())
                        I::TextUnformatted("<c=#4><no tokens in the file></c>");
                    else
                        I::TextUnformatted(tokens.c_str());
                    tokens.clear();
                    g_writeTokensTargets = &tokens;
                }
                if (scoped::TabItem(ICON_FA_FONT_CASE " Strings", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
                {
                    static std::string strings;
                    if (I::Button(ICON_FA_COPY " Copy All"))
                        I::SetClipboardText(strings.c_str());
                    if (strings.empty())
                        I::TextUnformatted("<c=#4><no strings in the file></c>");
                    else
                        I::TextUnformatted(strings.c_str());
                    strings.clear();
                    g_writeStringsTargets = &strings;
                }
            }
        }

        for (auto const& chunk : *PackFile)
        {
            std::string const fcc { (char const*)&chunk.Header.Magic, 4 };
            auto const* p = chunk.Data;
            I::TextUnformatted(std::format("Chunk <{}>", fcc.c_str()).c_str());
            I::Dummy({ 25, 0 });
            I::SameLine();
            if (scoped::Group())
                if (auto const chunkVersions = G::Game.Pack.GetChunk(fcc))
                    if (auto const itrChunkVersion = chunkVersions->find(chunk.Header.Version); itrChunkVersion != chunkVersions->end())
                        DrawPackFileType(p, PackFile->Header.Is64Bit, itrChunkVersion->second);
        }
    }
    void DrawPreview() override
    {
        uint32 index = 0;
        for (auto const& chunk : *PackFile)
        {
            if (ChunkPreview.size() <= index)
                ChunkPreview.resize(index + 1);

            auto& preview = ChunkPreview[index++];
            if (!preview)
                if (auto const itr = GetPackFileChunkPreviewRegistry().find(chunk.Header.Magic); itr != GetPackFileChunkPreviewRegistry().end())
                    preview.reset(itr->second());

            if (preview)
                preview->DrawPreview(PackFile->QueryChunk(chunk.Header.Magic));
        }
    }
};

}
