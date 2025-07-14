module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.Data.Texture;
import GW2Viewer.Common;
import std;

export namespace GW2Viewer::Data::Texture
{

struct Texture
{
    Texture(ImTextureRef handle, uint32 width, uint32 height) : Handle(handle), Width(width), Height(height) { }
    ~Texture();

    Texture(Texture const&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture const&) = delete;
    Texture& operator=(Texture&&) = delete;

    ImTextureRef const Handle;
    uint32 const Width;
    uint32 const Height;
};

struct LoadTextureOptions
{
    std::vector<byte> const* DataSource = nullptr;
    std::filesystem::path ExportPath;
};

struct TextureEntry : std::enable_shared_from_this<TextureEntry>
{
    uint32 FileID;
    std::string Format;
    std::vector<byte> Data;
    LoadTextureOptions Options;

    enum class TextureLoadingStates
    {
        NotLoaded,
        Queued,
        Loading,
        Loaded,
        Error,
    };
    std::unique_ptr<Texture> Texture;
    TextureLoadingStates TextureLoadingState = TextureLoadingStates::NotLoaded;
};

}
