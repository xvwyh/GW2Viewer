module;
#include "dep/gw2browser/src/ANetStructs.h"
#include "dep/gw2browser/src/Readers/ImageReader.h"
#include <concurrentqueue/blockingconcurrentqueue.h>
#include <DirectXTex.h>
#include <png.h>

module GW2Viewer.Data.Media.Texture.Manager;
import GW2Viewer.Data.Game;

void StripPNGMetadata(std::filesystem::path const& path)
{
    static constexpr png_color_8 sBIT { 8, 8, 8, 8, 8 };

    FILE* fp = nullptr;
    png_structp png = nullptr;
    png_infop info = nullptr;
    png_uint_32 width, height;
    // ReSharper disable once CppTooWideScope
    std::vector<std::unique_ptr<png_byte[]>> rows;
    std::vector<png_bytep> pointers;

    // Read the PNG written by WIC
    {
        auto cleanup = gsl::finally([&fp, &png, &info]
        {
            if (info)
                png_destroy_read_struct(&png, &info, nullptr);
            else if (png)
                png_destroy_read_struct(&png, nullptr, nullptr);
            png = nullptr;
            info = nullptr;

            if (fp)
                (void)fclose(fp);
            fp = nullptr;
        });

        if (png_byte header[8];
            _wfopen_s(&fp, path.c_str(), L"rb") ||
            fread(header, sizeof(*header), std::size(header), fp) != std::size(header) ||
            png_sig_cmp(header, 0, std::size(header)) ||
            !((png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr))) ||
            !((info = png_create_info_struct(png))) || 
            setjmp(png_jmpbuf(png)))
            return;

        png_init_io(png, fp);
        png_set_sig_bytes(png, 8);
        png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_NEVER, nullptr, 0);
        png_read_info(png, info);

        if (!((width = png_get_image_width(png, info))) ||
            !((height = png_get_image_height(png, info))) ||
            png_get_color_type(png, info) != PNG_COLOR_TYPE_RGB_ALPHA ||
            png_get_bit_depth(png, info) != 8)
            return;

        rows.resize(height);
        for (auto const size = png_get_rowbytes(png, info); auto& row : rows)
            row = std::make_unique<png_byte[]>(size);

        pointers.assign_range(rows | std::views::transform(&decltype(rows)::value_type::get));
        png_read_image(png, pointers.data());
    }

    // Write the minimal png if everything was read and cleaned up properly
    if (!fp && !png && !info && width && height && !pointers.empty())
    {
        auto cleanup = gsl::finally([&fp, &png, &info]
        {
            if (info)
                png_destroy_write_struct(&png, &info);
            else if (png)
                png_destroy_write_struct(&png, nullptr);
            png = nullptr;
            info = nullptr;

            if (fp)
                (void)fclose(fp);
            fp = nullptr;
        });

        if (_wfopen_s(&fp, path.c_str(), L"wb") ||
            !((png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr))) ||
            !((info = png_create_info_struct(png))) ||
            setjmp(png_jmpbuf(png)))
            return;

        png_init_io(png, fp);
        png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_set_sBIT(png, info, &sBIT);
        png_write_info(png, info);
        png_write_image(png, pointers.data());
        png_write_end(png, nullptr);
    }
}

namespace GW2Viewer::Data::Media::Texture
{

std::unique_ptr<Texture> Manager::Create(uint32 width, uint32 height, void const* data)
{
    auto* device = G::GraphicsDevice;
    if (!device)
        return nullptr;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource;
    ZeroMemory(&subResource, sizeof(subResource));
    subResource.pSysMem = data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;

    ID3D11Texture2D* tex = nullptr;
    if (device->CreateTexture2D(&desc, subResource.pSysMem ? &subResource : nullptr, &tex) != S_OK)
        return nullptr;
    auto guard = gsl::finally([tex] { tex->Release(); });

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    ID3D11ShaderResourceView* ptr = nullptr;
    if (device->CreateShaderResourceView(tex, &srvDesc, &ptr) != S_OK || !ptr)
        return nullptr;

    return std::make_unique<Texture>((uintptr_t)ptr, width, height);
}

void Manager::Load(uint32 fileID, LoadTextureOptions const& options)
{
    std::scoped_lock _(m_mutex);
    auto [itr, added] = m_textures.try_emplace(fileID, nullptr);
    if (!added)
    {
        if (!options.ExportPath.empty())
        {
            TextureEntry temp;
            temp.FileID = fileID;
            if (options.DataSource)
                temp.Data = *options.DataSource;
            else
                temp.Data = G::Game.Archive.GetFile(fileID);
            temp.Options = options;
            GetTextureRGBAImage(temp);
        }
        return;
    }

    auto& texture = itr->second;
    texture = std::make_shared<TextureEntry>();
    texture->FileID = fileID;
    if (options.DataSource)
        texture->Data = *options.DataSource;
    texture->Options = options;
    texture->TextureLoadingState = TextureEntry::TextureLoadingStates::Queued;
    m_loadingQueue.enqueue(texture->weak_from_this());

    if (!m_loadingThread)
        m_loadingThread.emplace([this] { LoadingThread(); });
}

void Manager::UploadToGPU()
{
    if (!G::GraphicsDevice)
        return;

    std::pair<std::weak_ptr<TextureEntry>, std::unique_ptr<BoxedImage>> item;
    while (m_uploadQueue.try_dequeue(item))
    {
        auto& [texturePtr, boxedImage] = item;
        if (auto const texture = texturePtr.lock())
        {
            [&]
            {
                auto& image = *(DirectX::ScratchImage*)boxedImage->ScratchImage;
                texture->Texture = Create(image.GetMetadata().width, image.GetMetadata().height, image.GetPixels());
                texture->TextureLoadingState = texture->Texture ? TextureEntry::TextureLoadingStates::Loaded : TextureEntry::TextureLoadingStates::Error;
            }();
        }
    }
}

Manager::BoxedImage::~BoxedImage()
{
    delete (DirectX::ScratchImage*)ScratchImage;
}

std::unique_ptr<Manager::BoxedImage> Manager::GetTextureRGBAImage(TextureEntry& texture)
{
    auto const* device = G::GraphicsDevice;
    if (!device)
        return nullptr;

    auto& data = texture.Data;
    if (data.size() < 8)
        return nullptr;

    using namespace DirectX;

    auto image = std::make_unique<ScratchImage>();
    TexMetadata info { };
    switch (std::byteswap(*(uint32*)data.data()))
    {
        case 'DDS ':
        {
            //Format = "DDS";
            DDSMetaData dds { };
            if (FAILED(LoadFromDDSMemoryEx(data.data(), data.size(), DDS_FLAGS_NONE, &info, &dds, *image)))
                return nullptr;
            //if (dds.flags & 4)
            //    Format = std::format("DDS/{}", std::string_view((char*)&dds.fourCC, 4));
            break;
        }
        case '\x89PNG':
            //Format = "PNG";
            if (FAILED(LoadFromWICMemory(data.data(), data.size(), WIC_FLAGS_NONE, &info, *image)))
                return nullptr;
            break;
        case 'ATEX':
        case 'ATTX':
        case 'ATEC':
        case 'ATEP':
        case 'ATEU':
        case 'ATET':
        {
            struct HeaderATEX
            {
                uint32 FourCC;
                uint32 Format;
                uint16 Width;
                uint16 Height;
            } &header = *(HeaderATEX*)data.data();
            DXGI_FORMAT format;
            uint32 miscFlags2 = 0;
            //Format = std::format("{}/{}", std::string_view((char*)data.data(), 4), std::string_view((char*)&header.Format, 4));
            gw2b::DatFile datFile { };
            gw2b::ImageReader const imageReader(data, datFile, gw2b::ANetFileType::ANFT_ATEX);
            switch (std::byteswap(header.Format))
            {
                case 'DXT1': format = DXGI_FORMAT_BC1_UNORM; miscFlags2 |= TEX_ALPHA_MODE_PREMULTIPLIED; break;

                case 'DXT2': format = DXGI_FORMAT_BC2_UNORM; miscFlags2 |= TEX_ALPHA_MODE_PREMULTIPLIED; break;
                case 'DXT3': format = DXGI_FORMAT_BC2_UNORM; miscFlags2 |= TEX_ALPHA_MODE_STRAIGHT; break;
                case 'DXTN': format = DXGI_FORMAT_BC2_UNORM; miscFlags2 |= TEX_ALPHA_MODE_STRAIGHT; break;

                case 'DXT4': format = DXGI_FORMAT_BC3_UNORM; miscFlags2 |= TEX_ALPHA_MODE_PREMULTIPLIED; break;
                case 'DXT5': format = DXGI_FORMAT_BC3_UNORM; miscFlags2 |= TEX_ALPHA_MODE_STRAIGHT; break;

                case 'DXTL': format = DXGI_FORMAT_BC3_UNORM; miscFlags2 |= TEX_ALPHA_MODE_PREMULTIPLIED; break;

                case 'DXTA': format = DXGI_FORMAT_BC4_UNORM; miscFlags2 |= TEX_ALPHA_MODE_OPAQUE; break;
                case 'BC7X': format = DXGI_FORMAT_BC7_UNORM; miscFlags2 |= TEX_ALPHA_MODE_STRAIGHT; break;
                case '3DCX': format = DXGI_FORMAT_R8G8B8A8_UNORM; miscFlags2 |= TEX_ALPHA_MODE_OPAQUE; break;
                default:
                    return nullptr;
            }
            image->Initialize({
                .width = header.Width,
                .height = header.Height,
                .depth = 1,
                .arraySize = 1,
                .mipLevels = 1,
                .miscFlags = 0,
                .miscFlags2 = miscFlags2,
                .format = format,
                .dimension = TEX_DIMENSION_TEXTURE2D,
                });
            if (std::byteswap(header.Format) == '3DCX')
                imageReader.readATEX((gw2b::ImageReader::RGBA*)image->GetPixels());
            else
                imageReader.getDecompressedATEX(image->GetPixels());
            break;
        }
        default:
            return nullptr;
    }

    Image const* img = image->GetImage(0, 0, 0);
    if (!img)
        return nullptr;

    if (IsCompressed(img->format))
    {
        auto decompressed = std::make_unique<ScratchImage>();
        if (FAILED(Decompress(*img, DXGI_FORMAT_R8G8B8A8_UNORM, *decompressed)))
            return nullptr;

        image.swap(decompressed);
        img = image->GetImage(0, 0, 0);
        if (!img)
            return nullptr;
    }

    if (img->format != DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        auto converted = std::make_unique<ScratchImage>();
        if (FAILED(Convert(*img, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, *converted)))
            return nullptr;

        image.swap(converted);
    }

    if (auto const path = texture.Options.ExportPath; !path.empty())
        if (Image const* img = image->GetImage(0, 0, 0))
            if (/*!exists(path)*/true)
                if (SUCCEEDED(SaveToWICFile(*img, WIC_FLAGS_NONE, GetWICCodec(WIC_CODEC_PNG), path.c_str())))
                    StripPNGMetadata(path);

    return std::make_unique<BoxedImage>(image.release());
}

void Manager::LoadingThread()
{
    while (!m_loadingThreadExitRequested)
    {
        std::weak_ptr<TextureEntry> texturePtr;
        m_loadingQueue.wait_dequeue_timed(texturePtr, 500ms);

        if (auto const texture = texturePtr.lock())
        {
            bool const loading = [this, &texture = *texture]
            {
                if (texture.Data.empty())
                    texture.Data = G::Game.Archive.GetFile(texture.FileID);

                auto image = GetTextureRGBAImage(texture);
                return image && m_uploadQueue.enqueue({ texture.shared_from_this(), std::move(image) });
            }();
            texture->TextureLoadingState = loading ? TextureEntry::TextureLoadingStates::Loading : TextureEntry::TextureLoadingStates::Error;
            texture->Data = { };
        }
    }
}

}
