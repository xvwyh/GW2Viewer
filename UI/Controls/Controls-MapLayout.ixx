module;
#include "Utils.h"
#include "UI/ImGui/ImGui.h"
#include <imgui_impl_dx11.h>

export module GW2Viewer.UI.Controls:MapLayout;
import :ContentButton;
import :Texture;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Media.Texture;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.UI.Manager;
import GW2Viewer.Utils;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.Math;
import <boost/container/small_vector.hpp>;

using GW2Viewer::Data::Content::ContentObject;

namespace GW2Viewer
{

bool IsPointInsidePolygon(std::span<ImVec2 const> polygon, ImVec2 const& point)
{
    ImVec2 const* prev = &polygon.back();
    bool result = false;
    for (auto const& current : polygon)
    {
        if ((current.y < point.y && prev->y >= point.y || prev->y < point.y && current.y >= point.y) && (current.x <= point.x || prev->x <= point.x))
            result ^= current.x + (point.y - current.y) / (prev->y - current.y) * (prev->x - current.x) < point.x;
        prev = &current;
    }
    return result;
}

}

export namespace GW2Viewer::UI::Controls
{

struct MapLayout
{
    struct Backdrop
    {
        std::unique_ptr<Data::Pack::PackFile> PackFile;
        float Scale = 1.0f;
        bool Water = false;
        bool Interior = false;

        struct Layer
        {
            ImVec2 StrippedDims { };
            ImVec2 ScaledDims { };
            ImRect ContentsRect { };
            struct Image
            {
                uint32 TextureFileID;
                ImVec2 Coords;
                ImRect BoundingBox;
                std::string Tooltip;
            };
            std::vector<Image> Images;
        };
        std::vector<Layer> Layers;
    };
    struct ObjectBase
    {
        ImRect BoundingBox;

        std::string Tooltip;
        ObjectBase& SetTooltip(std::string tooltip) { Tooltip = tooltip; return* this; }

        boost::container::small_vector<ContentObject const*, 5> Sources;
        ObjectBase& AddSource(ContentObject const& source) { Sources.emplace_back(&source); return *this; }
    };
    struct IconBase
    {
        uint32 TextureFileID;
    };
    struct Icon : IconBase, ObjectBase
    {
        Icon(uint32 textureFileID, ImVec2 position, ImVec2 size) : IconBase { .TextureFileID = textureFileID }, ObjectBase { .BoundingBox = { position - size / 2, position + size / 2 } }
        {
            BoundingBox.Floor();
        }
    };
    struct SectorBase
    {
        std::vector<ImVec2> Points;
    };
    struct Sector : SectorBase, ObjectBase
    {
        Sector(std::vector<ImVec2>&& points) : SectorBase { .Points = std::move(points) }
        {
            BoundingBox = { Points.front(), Points.front() };
            for (auto const& point : Points | std::views::drop(1))
                BoundingBox.Add(point);
        }
    };

    void Reset()
    {
        *this = { };
    }
    void AddBackdrop(float scale, uint32 unexploredFileID, uint32 exploredFileID, bool water = false, bool interior = false);
    Icon& AddIcon(uint32 textureFileID, ContentObject& mapDef, ImVec2 mapPosition, float size) { return AddIcon(textureFileID, mapDef, mapPosition, { size, size }); }
    Icon& AddIcon(uint32 textureFileID, ContentObject& mapDef, ImVec2 mapPosition, ImVec2 size);
    Icon& AddIcon(uint32 textureFileID, ImVec2 position, float size) { return AddIcon(textureFileID, position, { size, size }); }
    Icon& AddIcon(uint32 textureFileID, ImVec2 position, ImVec2 size) { return Icons.emplace_back(textureFileID, position, size); }
    Sector& AddSector(std::vector<ImVec2>&& points) { return Sectors.emplace_back(std::move(points)); }
    void Initialize();

    ContentObject* MapLayoutContinent = nullptr;
    ContentObject* MapLayoutContinentFloor = nullptr;
    std::vector<Backdrop> UnexploredBackdrops;
    std::vector<Backdrop> ExploredBackdrops;
    std::vector<Icon> Icons;
    std::vector<Sector> Sectors;
    ImRect MapBoundingBox { };

    bool ExploredMaskDirty = false;
    std::vector<byte> ExploredMask;
    std::unique_ptr<Data::Media::Texture::Texture> ExploredMaskTexture;

    ImVec2 ViewportOffset { };
    float ViewportScale = 1.0f;
    float ViewportScaleChaseTarget = 1.0f;
    ImVec2 ViewportScaleScreenTarget { };

    bool Initialized = false;
    bool FirstDraw = false;

    struct ObjectSettings
    {
        static const ObjectSettings Default;
        bool Visible = true;
    };
    std::unordered_map<ContentObject const*, ObjectSettings> ObjectSettings;
    struct ObjectSettings const& GetObjectSettings(ContentObject const& object) const { auto const itr = ObjectSettings.find(&object); return itr != ObjectSettings.end() ? itr->second : ObjectSettings::Default; }
    struct ObjectSettings& SetObjectSettings(ContentObject const& object) { return ObjectSettings[&object]; }

    void Draw();
};

STATIC(MapLayout::ObjectSettings::Default);

void MapLayout::AddBackdrop(float scale, uint32 unexploredFileID, uint32 exploredFileID, bool water, bool interior)
{
    if (unexploredFileID)
        UnexploredBackdrops.emplace_back(G::Game.Archive.GetPackFile(unexploredFileID), scale);
    if (exploredFileID)
        ExploredBackdrops.emplace_back(G::Game.Archive.GetPackFile(exploredFileID), scale, water, interior);
}

MapLayout::Icon& MapLayout::AddIcon(uint32 textureFileID, ContentObject& mapDef, ImVec2 mapPosition, ImVec2 size)
{
    for (ContentObject& mapLayoutRegion : (*MapLayoutContinentFloor)["Regions->Region"])
    {
        for (ContentObject& mapLayoutMap : mapLayoutRegion["Maps->Map"])
        {
            ContentObject& mapDetailsMap = mapLayoutMap["Details"];
            ContentObject& map = mapDetailsMap["Map"];
            if (&map == &mapDef)
            {
                ImVec2 const& mapMin = mapLayoutMap["MapMin"];
                ImVec2 const& mapMax = mapLayoutMap["MapMax"];
                ImVec2 const& worldMapMin = mapLayoutMap["WorldMapMin"];
                ImVec2 const& worldMapMax = mapLayoutMap["WorldMapMax"];
                ImRect mapRect { { mapMin.x, mapMax.y } , { mapMax.x, mapMin.y } };
                ImRect worldMapRect { worldMapMin, worldMapMax };
                return AddIcon(textureFileID, worldMapRect.Min + worldMapRect.GetSize() * ((mapPosition - mapRect.Min) / mapRect.GetSize()), size);
            }
        }
    }
    return AddIcon(textureFileID, { }, size);
}

void MapLayout::Initialize()
{
    ContentObject& mapDetailsContinentFloor = (*MapLayoutContinentFloor)["Details"];
    AddBackdrop(32.0f, mapDetailsContinentFloor["UnexploredZoomLevel0"], mapDetailsContinentFloor["ExploredZoomLevel0"], true);
    AddBackdrop( 8.0f, mapDetailsContinentFloor["UnexploredZoomLevel1"], mapDetailsContinentFloor["ExploredZoomLevel1"], true);
    AddBackdrop( 4.0f, mapDetailsContinentFloor["UnexploredZoomLevel2"], mapDetailsContinentFloor["ExploredZoomLevel2"], true);
    AddBackdrop( 2.0f, mapDetailsContinentFloor["UnexploredZoomLevel3"], mapDetailsContinentFloor["ExploredZoomLevel3"], true);
    AddBackdrop( 1.0f, 0,                                                mapDetailsContinentFloor["ExploredZoomLevel4"], true);

    for (ContentObject& mapLayoutRegion : (*MapLayoutContinentFloor)["Regions->Region"])
    {
        for (ContentObject& mapLayoutMap : mapLayoutRegion["Maps->Map"])
        {
            ImVec2 const& mapMin = mapLayoutMap["MapMin"];
            ImVec2 const& mapMax = mapLayoutMap["MapMax"];
            ImVec2 const& worldMapMin = mapLayoutMap["WorldMapMin"];
            ImVec2 const& worldMapMax = mapLayoutMap["WorldMapMax"];
            auto convertPosition = [mapRect = ImRect { { mapMin.x, mapMax.y } , { mapMax.x, mapMin.y } }, worldMapRect = ImRect { worldMapMin, worldMapMax }](ImVec2 const& mapPosition)
            {
                return worldMapRect.Min + worldMapRect.GetSize() * ((mapPosition - mapRect.Min) / mapRect.GetSize());
            };

            auto fillSources = [&](ObjectBase& object) -> ObjectBase& { return object.AddSource(*MapLayoutContinentFloor).AddSource(mapLayoutRegion).AddSource(mapLayoutMap); };

            for (ContentObject& mapLayoutOutpost : mapLayoutMap["Outposts->Outpost"])
                fillSources(AddIcon(961377, convertPosition(mapLayoutOutpost["Position"]), 32)).AddSource(mapLayoutOutpost);

            for (ContentObject& mapLayoutOrrTemple : mapLayoutMap["OrrTemples->OrrTemple"])
                fillSources(AddIcon(347219, convertPosition(mapLayoutOrrTemple["Position"]), 32)).AddSource(mapLayoutOrrTemple);

            for (ContentObject& mapLayoutInteriorColor : mapLayoutMap["Interiors->Interior"])
            {
                auto const& worldMapMin = convertPosition(mapLayoutInteriorColor["Min"]);
                auto const& worldMapMax = convertPosition(mapLayoutInteriorColor["Max"]);
                ImRect rect { { worldMapMin.x, worldMapMax.y }, { worldMapMax.x, worldMapMin.y } };
                AddBackdrop(1.0f, 0, mapLayoutInteriorColor["Texture"], false, true);
            }

            for (ContentObject& mapLayoutPacingTask : mapLayoutMap["PacingTasks->PacingTask"])
                fillSources(AddIcon(102439, convertPosition(mapLayoutPacingTask["VendorPosition"]), 32)).AddSource(mapLayoutPacingTask);

            for (ContentObject& mapLayoutPointOfInterest : mapLayoutMap["PointsOfInterest->PointOfInterest"])
            {
                ContentObject& pointOfInterestDef = mapLayoutPointOfInterest["PointOfInterest"];
                uint32 textureFileID = 0;
                ImVec2 const size { 32, 32 };
                switch ((uint32)pointOfInterestDef["Type"])
                {
                    case 0: textureFileID = 97461; break;
                    case 1: textureFileID = 102348; break; // Res Shrine
                    case 2:
                    {
                        for (ContentObject* markerDef : pointOfInterestDef["Marker::Details->Marker"])
                            if (markerDef)
                                textureFileID = (*markerDef)["MapIcon"];
                        break;
                    }
                    case 3: textureFileID = 347213; break;
                }
                if (textureFileID)
                    fillSources(AddIcon(textureFileID, convertPosition(mapLayoutPointOfInterest["Position"]), size))
                        .AddSource(mapLayoutPointOfInterest)
                        .SetTooltip(Utils::Encoding::ToUTF8(std::format(L"{}\nMap: {}\nRegion: {}\nFloor: {}", mapLayoutPointOfInterest.GetDisplayName(), mapLayoutMap.GetDisplayName(), mapLayoutRegion.GetDisplayName(), MapLayoutContinentFloor->GetDisplayName())));
            }

            for (ContentObject& mapLayoutSector : mapLayoutMap["Sectors->Sector"])
                fillSources(AddSector({ std::from_range, mapLayoutSector["Polygon->Point"] | std::views::transform(convertPosition) })).AddSource(mapLayoutSector);

            for (ContentObject& mapLayoutSkillChallenge : mapLayoutMap["SkillChallenges->SkillChallenge"])
                fillSources(AddIcon(102601, convertPosition(mapLayoutSkillChallenge["Position"]), 32)).AddSource(mapLayoutSkillChallenge);

            for (ContentObject& mapLayoutQuestTarget : mapLayoutMap["QuestTargets->QuestTarget"])
                fillSources(AddIcon(102369, convertPosition(mapLayoutQuestTarget["Position"]), 32)).AddSource(mapLayoutQuestTarget);

            for (ContentObject& mapLayoutTrainingPoint : mapLayoutMap["TrainingPoints->TrainingPoint"])
            {
                ContentObject& trainingPoint = mapLayoutTrainingPoint["TrainingPoint"];
                ContentObject& trainingCategory = trainingPoint["Category"];
                fillSources(AddIcon(trainingCategory["MapIcon"], convertPosition(mapLayoutTrainingPoint["Position"]), 32)).AddSource(mapLayoutTrainingPoint);
            }
        }
    }

    for (auto* backdrops : { &UnexploredBackdrops, &ExploredBackdrops })
    {
        for (auto& backdrop : *backdrops)
        {
            auto chunk = backdrop.PackFile->QueryChunk(fcc::PGTB);

            backdrop.Layers.reserve(chunk["layers"].GetArraySize());
            for (auto const& layerData : chunk["layers"])
            {
                auto& layer = backdrop.Layers.emplace_back();
                layer.StrippedDims = layerData["strippedDims"];
                backdrop.Scale *= 512 / layer.StrippedDims.x;
            }

            for (auto& layer : backdrop.Layers)
                layer.ScaledDims = layer.StrippedDims * backdrop.Scale;

            uint32 i = 0;
            for (auto const& pageData : chunk["strippedPages"])
            {
                auto& layer = backdrop.Layers.at(pageData["layer"]);
                auto& image = layer.Images.emplace_back();
                image.TextureFileID = pageData["filename"];
                image.Coords = pageData["coord"];
                image.BoundingBox = { image.Coords * layer.ScaledDims, image.Coords * layer.ScaledDims + layer.ScaledDims };
                image.Tooltip = std::format("strippedPages[{}]\ncoords: {}, {}\nfilename: {}", i++, image.Coords.x, image.Coords.y, image.TextureFileID);

                if (layer.ContentsRect.Min == layer.ContentsRect.Max)
                    layer.ContentsRect = image.BoundingBox;
                else
                    layer.ContentsRect.Add(image.BoundingBox);
            }

            for (auto const& layer : backdrop.Layers)
                if (MapBoundingBox.Min == MapBoundingBox.Max)
                    MapBoundingBox = layer.ContentsRect;
                else
                    MapBoundingBox.Add(layer.ContentsRect);
        }
    }

    static constexpr auto maskSize = 1024;
    ExploredMaskDirty = true;
    ExploredMask.resize(maskSize * maskSize * 4, 0x00);
    for (auto const& sector : Sectors)
    {
        ImRect boundingBox;
        std::vector maskPoints { std::from_range, sector.Points | std::views::transform([this](ImVec2 point) { return ImFloor(point / MapBoundingBox.Max * maskSize - ImVec2(0.5f, 0.5f)); }) };
        for (auto& point : maskPoints)
            if (boundingBox.Min == ImVec2() && boundingBox.Max == ImVec2())
                boundingBox = { point, point };
            else
                boundingBox.Add(point);
        for (float x = boundingBox.Min.x - 1; x <= boundingBox.Max.x + 1; ++x)
        for (float y = boundingBox.Min.y - 1; y <= boundingBox.Max.y + 1; ++y)
            if (IsPointInsidePolygon(maskPoints, { x, y }))
                ExploredMask[(y * maskSize + x) * 4] = 0xFF;
    }
    
    FirstDraw = true;
    Initialized = true;
}

void MapLayout::Draw()
{
    if (!Initialized)
        std::terminate();

    uint32 const exploredMaskSize = std::sqrt(ExploredMask.size() / 4);
    if (std::exchange(ExploredMaskDirty, false))
        ExploredMaskTexture = G::Game.Texture.Create(exploredMaskSize, exploredMaskSize, ExploredMask.data());

    static struct PixelBuffer
    {
        ImVec2 DisplaySize;
        ImVec2 MousePosition;
        float WaterThresholdLow = 0.0f;
        float WaterThresholdHigh = 0.3f;
        alignas(float) bool UseMask = false;
        alignas(float) bool UseWater = false;
    } pixelBuffer { };
    static_assert(sizeof(PixelBuffer) % 16 == 0); // Shader requirement to buffers

    static auto buffer = ImGui_ImplDX11_CreateBuffer(sizeof(PixelBuffer));
    static auto shader = ImGui_ImplDX11_CompilePixelShader(R"(
cbuffer pixelBuffer : register(b0)
{
    float2 DisplaySize;
    float2 MousePosition;
    float WaterThresholdLow;
    float WaterThresholdHigh;
    bool UseMask;
    bool UseWater;
};
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
};
sampler sampler0 : register(s0);
Texture2D texture0 : register(t0);
Texture2D texture1 : register(t1);

float Rescale(float value, float minVal, float maxVal)
{
    return saturate((value - minVal) / (maxVal - minVal));
}
float4 main(PS_INPUT input) : SV_Target
{
    float4 tex = texture0.Sample(sampler0, input.uv);
    float4 color = tex;
    if (UseWater)
    {
        color.a = 1.0f;
        float distanceToMouse = length(input.pos.xy - MousePosition);
        color = lerp(color, lerp(float4(100/255.0f, 143/255.0f, 107/255.0f, 1.0f), float4(12/255.0f, 63/255.0f, 53/255.0f, 1.0f), tex.a), saturate(distanceToMouse / 200.0f) * Rescale(tex.a, WaterThresholdLow, WaterThresholdHigh));
    }
    if (UseMask)
        color.a *= texture1.Sample(sampler0, input.uv2).r;
    return input.col * color;
}
)");

    auto const cursor = I::GetCursorScreenPos();
    auto const viewportSize = I::GetContentRegionAvail();
    if (FirstDraw)
        ViewportOffset = MapBoundingBox.GetCenter() - viewportSize / 2;

    ImRect const viewportScreenRect { cursor, cursor + viewportSize };
    ImRect viewportWorldRect { ViewportOffset - viewportSize / ViewportScale / 2, ViewportOffset + viewportSize / ViewportScale / 2 };
    auto project = [&](ImVec2 const& worldPosition) { return viewportScreenRect.Min + (worldPosition - viewportWorldRect.Min) * ViewportScale; };
    auto unproject = [&](ImVec2 const& screenPosition) { return (screenPosition - viewportScreenRect.Min) / ViewportScale + viewportWorldRect.Min; };

    scoped::Child("Viewport", viewportSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    I::InvisibleButton("Canvas", viewportSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    if (I::IsItemHovered() && I::GetIO().MouseWheel != 0.0f)
    {
        ViewportScaleChaseTarget *= std::exp((I::GetIO().KeyShift ? 0.8f : 0.4f) * I::GetIO().MouseWheel);
        ViewportScaleScreenTarget = I::GetMousePos();
    }
    if (std::fabs(ViewportScaleChaseTarget - 1.0f) < 0.001f)
        ViewportScaleChaseTarget = 1.0f;

    auto const oldMouseWorldPosition = unproject(ViewportScaleScreenTarget);
    if (std::fabs(ViewportScale - ViewportScaleChaseTarget) < 0.00001f)
        ViewportScale = ViewportScaleChaseTarget;
    else
        ViewportScale = Utils::Math::ExpDecay(ViewportScale, ViewportScaleChaseTarget, 15.0f, G::UI.DeltaTime());
    viewportWorldRect = { ViewportOffset - viewportSize / ViewportScale / 2, ViewportOffset + viewportSize / ViewportScale / 2 };
    ViewportOffset -= unproject(ViewportScaleScreenTarget) - oldMouseWorldPosition;

    if (ImGuiMouseButton button; I::IsItemActive() && (I::IsMouseDragging(button = ImGuiMouseButton_Left) || I::IsMouseDragging(button = ImGuiMouseButton_Middle) || I::IsMouseDragging(button = ImGuiMouseButton_Right)))
    {
        float const scale = (I::GetIO().KeyShift ? 5.0f : 1.0f) * (I::GetIO().KeyCtrl ? 10.0f : 1.0f);
        ViewportOffset -= I::GetMouseDragDelta(button) * scale / ViewportScale;
        I::ResetMouseDragDelta(button);
    }
    viewportWorldRect = { ViewportOffset - viewportSize / ViewportScale / 2, ViewportOffset + viewportSize / ViewportScale / 2 };

    I::GetWindowDrawList()->AddCallback([](ImDrawList const* parent_list, ImDrawCmd const* cmd)
    {
        ImGui_ImplDX11_SetPixelShader(shader);
        ImGui_ImplDX11_SetPixelShaderShaderResource(1, cmd->UserCallbackData);
    }, (void*)ExploredMaskTexture->Handle.GetTexID());
    for (auto const* backdrops : { &UnexploredBackdrops, &ExploredBackdrops })
    {
        bool first = true;
        for (auto const& backdrop : *backdrops)
        {
            float alpha = std::clamp(Utils::Math::Remap(1.0f / ViewportScale, backdrop.Scale, backdrop.Scale * 2, 1.0f, 0.0f), 0.0f, 1.0f);
            if (std::exchange(first, false) && 1.0 / ViewportScale > backdrop.Scale)
                alpha = 1;
            if (alpha <= 0)
                continue;

            pixelBuffer.UseMask = !backdrop.Interior && backdrops == &ExploredBackdrops;
            pixelBuffer.UseWater = backdrop.Water;
            using Param = std::tuple<bool, bool>;
            I::GetWindowDrawList()->AddCallback([](ImDrawList const* parent_list, ImDrawCmd const* cmd)
            {
                auto const param = (Param const*)cmd->UserCallbackData;
                std::tie(pixelBuffer.UseMask, pixelBuffer.UseWater) = *param;
                delete param;
                ImGui_ImplDX11_SetPixelShaderConstantBuffer(buffer, &pixelBuffer, sizeof(pixelBuffer));
            }, new Param { pixelBuffer.UseMask, pixelBuffer.UseWater });

            for (auto const& layer : backdrop.Layers)
                for (auto const& image : layer.Images)
                    if (viewportWorldRect.Overlaps(image.BoundingBox))
                        if (scoped::WithCursorScreenPos(ImRoundIf(project(image.BoundingBox.Min), ViewportScale == 1.0f)))
                        {
                            auto c = I::GetCursorScreenPos();
                            Texture(image.TextureFileID,
                            {
                                .Color = { 1, 1, 1, alpha },
                                .Size = image.BoundingBox.GetSize() * ViewportScale,
                                .UV2 = ImRect { image.BoundingBox.Min / MapBoundingBox.Max, image.BoundingBox.Max / MapBoundingBox.Max },
                                .FullPreviewOnHover = false,
                                .AdvanceCursor = false
                            });
                            if (I::IsItemHovered())
                                I::GetWindowDrawList()->AddRect(c, c + image.BoundingBox.GetSize() * ViewportScale, 0xFF00FF00, 0, ImDrawFlags_None, 5);
                            if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
                                I::TextUnformatted(image.Tooltip.c_str());
                        }
        }
    }
    I::GetWindowDrawList()->AddCallback([](ImDrawList const* parent_list, ImDrawCmd const* cmd)
    {
        ImGui_ImplDX11_SetPixelShaderShaderResource(1, nullptr);
    }, nullptr);
    I::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    //if (scoped::WithCursorScreenPos(viewportScreenRect.Min + (-viewportWorldRect.Min + MapBoundingBox.Min) * ViewportScale))
    //    I::Image(ExploredMaskTexture->Handle, MapBoundingBox.GetSize()* ViewportScale);

    struct LayoutTreeNode
    {
        std::wstring Name;
        ObjectBase const* Object = nullptr;
        uint32 TextureFileID = 0;
        bool Visible = true;
        std::unordered_map<ContentObject const*, LayoutTreeNode> Children;
        void Add(ObjectBase const& object, uint32 textureFileID = 0)
        {
            if (object.Sources.empty())
                return;
            LayoutTreeNode* parent = this;
            for (auto source : object.Sources)
            {
                parent = &parent->Children[source];
                if (parent->Name.empty())
                    parent->Name = source->GetDisplayName();
            }
            parent->Object = &object;
            parent->TextureFileID = textureFileID;
        }
    };
    struct LayoutTree
    {
        LayoutTreeNode Sectors { L"Sectors" };
        LayoutTreeNode Icons { L"Icons" };
        LayoutTreeNode* begin() { return &Sectors; }
        LayoutTreeNode* end() { return &Icons + 1; }
        void clear()
        {
            for (auto& node : *this)
                node.Children.clear();
        }
    };
    static LayoutTree tree;
    tree.clear();

    for (auto const& sector : Sectors)
    {
        if (viewportWorldRect.Overlaps(sector.BoundingBox))
        {
            tree.Sectors.Add(sector);
            if (std::ranges::any_of(sector.Sources, [this](auto source) { return !GetObjectSettings(*source).Visible; }))
                continue;

            for (auto const& point : sector.Points)
                I::GetWindowDrawList()->PathLineTo(project(point));
            I::GetWindowDrawList()->PathLineTo(project(sector.Points.front()));
            if (IsPointInsidePolygon(sector.Points, unproject(I::GetMousePos())))
            {
                auto const size = I::GetWindowDrawList()->_Path.Size;
                I::GetWindowDrawList()->PathFillConcave(0x10FFFFFF);
                I::GetWindowDrawList()->_Path.Size = size;
            }
            I::GetWindowDrawList()->PathStroke(0x40FFFFFF, ImDrawFlags_None, 2);
        }
    }
    
    for (auto const& icon : Icons)
    {
        if (viewportWorldRect.Overlaps(icon.BoundingBox))
        {
            tree.Icons.Add(icon, icon.TextureFileID);
            if (std::ranges::any_of(icon.Sources, [this](auto source) { return !GetObjectSettings(*source).Visible; }))
                continue;

            float const scale = std::clamp(ViewportScale, 0.5f, 1.0f);
            if (scoped::WithCursorScreenPos(ImRoundIf(project(icon.BoundingBox.GetCenter()) - icon.BoundingBox.GetSize() / 2 * scale, scale == 1.0f)))
            {
                Texture(icon.TextureFileID, { .Size = icon.BoundingBox.GetSize() * scale, .FullPreviewOnHover = false, .AdvanceCursor = false });
                if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
                    I::TextUnformatted(icon.Tooltip.c_str());
            }
        }
    }

    // Draw UI
    if (scoped::WithCursorPos(0, 0))
    if (scoped::Child("Settings", { 200, -FLT_MIN }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
    {
        pixelBuffer.DisplaySize = I::GetIO().DisplaySize;
        pixelBuffer.MousePosition = I::GetIO().MousePos;
        I::DragFloat("WaterThresholdLow", &pixelBuffer.WaterThresholdLow, 0.01f, 0, 1);
        I::DragFloat("WaterThresholdHigh", &pixelBuffer.WaterThresholdHigh, 0.01f, 0, 1);
        I::DragFloat2("ViewportOffset", &ViewportOffset[0], 25);
        if (I::DragFloat("ViewportScale", &ViewportScale, 0.01f, 0.001f, 5, "%.3f", ImGuiSliderFlags_Logarithmic))
            ViewportScaleChaseTarget = ViewportScale;

        if (scoped::WithStyleVar(ImGuiStyleVar_IndentSpacing, 12))
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("ObjectSettings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit))
        {
            I::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupColumn("1", ImGuiTableColumnFlags_WidthFixed, I::GetFrameHeight());
            I::TableSetupColumn("2", ImGuiTableColumnFlags_WidthFixed, I::GetFrameHeight());

            auto renderTree = [this](ContentObject const* source, LayoutTreeNode& node, bool parentVisible, bool& makeParentVisible, auto& renderTree) -> void
            {
                I::TableNextRow();
                I::TableNextColumn();
                void const* id = source ? (void const*)source : &node;
                ImGuiTreeNodeFlags_ const leaf = node.Children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_None;
                bool const open = I::TreeNodeEx(id, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap | leaf, "");
                bool visible = source ? GetObjectSettings(*source).Visible : node.Visible;
                if (scoped::WithID(id))
                {
                    I::SameLine();
                    auto color = I::GetStyleColorVec4(ImGuiCol_CheckMark);
                    color.w *= parentVisible ? 1.0f : 0.25f;
                    if (scoped::WithColorVar(ImGuiCol_CheckMark, color))
                    if (I::Checkbox("##Visible", &visible))
                    {
                        if (!parentVisible)
                        {
                            visible = true;
                            makeParentVisible = true;
                        }
                        
                        if (source)
                            SetObjectSettings(*source).Visible = visible;
                        else
                            node.Visible = visible;
                    }
                    if (node.TextureFileID)
                    {
                        I::SameLine(0, 0);
                        Texture(node.TextureFileID, { .Size = I::GetFrameSquare() });
                    }
                    I::SameLine(0, 0);
                    if (source)
                    {
                        ContentButtonOptions::CondenseContext condense { .FullName = true, .TypeName = true };
                        ContentButton((ContentObject*)source, id, { .SharedCondenseContext = &condense }); // TODO: Fix constness
                    }
                    else
                        I::Text("%s (%zu)", Utils::Encoding::ToUTF8(node.Name).c_str(), node.Visible);

                    I::TableNextColumn();
                    if (node.Object && I::Button(ICON_FA_LOCATION_CROSSHAIRS, I::GetFrameSquare()))
                    {
                        ViewportOffset = node.Object->BoundingBox.GetCenter();
                        ViewportScale = ViewportScaleChaseTarget = 1.0f;
                    }
                    I::TableNextColumn(); I::Button(ICON_FA_PERSON_WALKING, I::GetFrameSquare());
                }

                if (open)
                {
                    bool makeThisVisible = false;
                    std::vector sorted { std::from_range, node.Children | std::views::transform([](auto&& value) { return &value; }) };
                    std::ranges::sort(sorted, [&](auto const a, auto const b)
                    {
                        if (auto result = a->second.TextureFileID <=> b->second.TextureFileID; result != std::strong_ordering::equal)
                            return result == std::strong_ordering::less;
                        return _wcsicmp(a->second.Name.c_str(), b->second.Name.c_str()) < 0;
                    });
                    for (auto const child : sorted)
                        renderTree(child->first, child->second, parentVisible && visible, makeThisVisible, renderTree);

                    if (makeThisVisible)
                    {
                        if (!parentVisible)
                            makeParentVisible = true;

                        if (source)
                            SetObjectSettings(*source).Visible = true;
                        else
                            node.Visible = true;
                    }
                    I::TreePop();
                }
            };

            bool makeParentVisible = false;
            for (auto& node : tree)
                renderTree(nullptr, node, true, makeParentVisible, renderTree);
        }
    }

    FirstDraw = false;
}


}
