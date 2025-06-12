#pragma once
#include "Content.h"
#include "PackFileLayoutTraversal.h"
#include "Texture.h"

#include "dep/imgui/imgui.h"
#include "dep/imgui/imgui_internal.h"

struct MapLayoutViewer
{
    struct Backdrop
    {
        std::unique_ptr<pf::PackFile> PackFile;
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
    std::unique_ptr<Texture> ExploredMaskTexture;

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
