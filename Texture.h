#pragma once
#include "Common.h"

#include <memory>
#include <string>
#include <vector>

namespace DirectX
{
    class ScratchImage;
}

struct Texture
{
    Texture(void* handle, uint32 width, uint32 height) : Handle(handle), Width(width), Height(height) { }
    ~Texture();

    Texture(Texture const&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture const&) = delete;
    Texture& operator=(Texture&&) = delete;

    void* const Handle;
    uint32 const Width;
    uint32 const Height;
};

struct LoadTextureOptions
{
    std::vector<byte> const* DataSource = nullptr;
    bool Export = false;
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
    std::unique_ptr<DirectX::ScratchImage> GetRGBAImage() const;
};

TextureEntry const* GetTexture(uint32 fileID);
std::unique_ptr<Texture> CreateTexture(uint32 width, uint32 height, void const* data = nullptr);
void LoadTexture(uint32 fileID, LoadTextureOptions const& options = { });
void UploadLoadedTexturesToGPU();
void StopLoadingTextures();
