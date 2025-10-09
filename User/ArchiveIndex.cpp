module;
#include <Windows.h>

module GW2Viewer.User.ArchiveIndex;
import <cctype>;

namespace GW2Viewer::User
{

bool ArchiveIndex::UpdateCache(CacheFile& cache, uint32 fileID) const
{
    auto const entry = m_archiveSource->Archive.GetFileMftEntry(fileID);
    if (!entry)
        return false;

    auto const expected = GetExpectedCacheFile(fileID);

    cache =
    {
        .IsRevision = expected.IsRevision,
        .IsStream = expected.IsStream,
        .AddedTimestampIndex = cache.AddedTimestampIndex,
        .ChangedTimestampIndex = cache.ChangedTimestampIndex,
        .RawFileSize = entry->alloc.size,
        .FileSize = m_archiveSource->Archive.GetFileSize(fileID),
        .BaseOrFileID = expected.BaseOrFileID,
        .ParentOrStreamBaseID = expected.ParentOrStreamBaseID,
    };
    if (m_header->NumFiles < fileID + 1)
        m_header->NumFiles = fileID + 1;

    try
    {
        std::array<byte, 1024> data;
        uint32 readBytes = 0;
        auto read = [&](uint32 bytes)
        {
            if (bytes > readBytes)
                readBytes = m_archiveSource->Archive.GetFile(fileID, { data.data(), std::min<uint32>(data.size(), bytes) }, true);
            return readBytes;
        };
        if (read(32) < sizeof(uint32))
        {
            cache.MetadataIndex = AddMetadata({ .Type = Type::Unknown });
            return true;
        }

        CacheMetadata metadata { .Type = Type::Uncategorized };

        metadata.FourCC = *(uint32 const*)data.data();
        uint32 const fccBackup = metadata.FourCC;
        switch ((fcc)metadata.FourCC)
        {
            case fcc::asnd:
            case fcc::OggS:
                metadata.Type = Type::Audio;
                break;
            case fcc::ATEX:
            case fcc::ATTX:
            case fcc::ATEC:
            case fcc::ATEP:
            case fcc::ATEU:
            case fcc::ATET:
            case fcc::CTEX:
            {
                metadata.Type = Type::Texture;

                struct Header
                {
                    uint32 FourCC;
                    uint32 Format;
                    uint16 Width;
                    uint16 Height;
                } const& header = *(Header const*)data.data();
                metadata.Texture.Format = header.Format;
                metadata.Texture.Width = header.Width;
                metadata.Texture.Height = header.Height;
                break;
            }
            case fcc::DDS:
            {
                metadata.Type = Type::Texture;

                struct DDS_PIXELFORMAT
                {
                    uint32 size;
                    uint32 flags;
                    uint32 fourCC;
                    uint32 RGBBitCount;
                    uint32 RBitMask;
                    uint32 GBitMask;
                    uint32 BBitMask;
                    uint32 ABitMask;
                };
                struct DDS_HEADER
                {
                    uint32 size;
                    uint32 flags;
                    uint32 height;
                    uint32 width;
                    uint32 pitchOrLinearSize;
                    uint32 depth; // only if DDS_HEADER_FLAGS_VOLUME is set in flags
                    uint32 mipMapCount;
                    uint32 reserved1[11];
                    DDS_PIXELFORMAT ddspf;
                    uint32 caps;
                    uint32 caps2;
                    uint32 caps3;
                    uint32 caps4;
                    uint32 reserved2;
                };
                read(sizeof(DDS_HEADER));
                auto const& header = *(DDS_HEADER const*)&data[4];
                metadata.Texture.Format = header.ddspf.flags & 4 ? header.ddspf.fourCC : std::byteswap('DDS\0');
                metadata.Texture.Width = header.width;
                metadata.Texture.Height = header.height;
                break;
            }
            case fcc::PNG:
            {
                metadata.Type = Type::Texture;

                struct Header
                {
                    uint32 FourCC;
                    uint32 LineEndings;
                    struct
                    {
                        uint32 Size;
                        uint32 Type;
                        struct
                        {
                            uint32 Width;
                            uint32 Height;
                        } IHDR;
                    } Chunk;
                } const& header = *(Header const*)data.data();
                if (header.Chunk.Type == std::byteswap('IHDR'))
                {
                    metadata.Texture.Format = std::byteswap('PNG\0');
                    metadata.Texture.Width = std::byteswap(header.Chunk.IHDR.Width);
                    metadata.Texture.Height = std::byteswap(header.Chunk.IHDR.Height);
                }
                break;
            }
            case fcc::RIFF:
            {
#pragma pack(push, 1)
                struct Header
                {
                    uint32 FourCC;
                    uint32 FileSize;
                    uint32 Format;
                    struct
                    {
                        uint32 Type;
                        uint32 Size;
                        struct
                        {
                            uint32 Flags;
                            byte Width[3];
                            byte Height[3];
                        } VP8X;
                        static_assert(sizeof(VP8X) == 4 + 3 + 3);
                    } Chunk;
                } const& header = *(Header const*)data.data();
                static_assert(sizeof(header.Chunk.VP8X) == 4 + 3 + 3);
#pragma pack(pop)
                if (header.Format == std::byteswap('WEBP'))
                {
                    metadata.Type = Type::Texture;
                    metadata.Texture.Format = header.Format;
                    if (header.Chunk.Type == std::byteswap('VP8X'))
                    {
                        metadata.Texture.Width = (header.Chunk.VP8X.Width[0] | header.Chunk.VP8X.Width[1] << 8 | header.Chunk.VP8X.Width[2] << 16) + 1;
                        metadata.Texture.Height = (header.Chunk.VP8X.Height[0] | header.Chunk.VP8X.Height[1] << 8 | header.Chunk.VP8X.Height[2] << 16) + 1;
                    }
                }
                break;
            }
            case fcc::strs:
                metadata.Type = Type::StringFile;
                break;
            case fcc::TTF:
            case fcc::wOFF:
                metadata.Type = Type::FontFile;
                break;
            default:
                switch ((metadata.FourCC &= 0xFFFFFF) << 8)
                {
                    case std::byteswap('ID3'): // MP3
                        metadata.Type = Type::Audio;
                        break;
                    case '\0\xEF\xBB\xBF': // UTF-8 BOM
                        metadata.Type = Type::Text;
                        break;
                    case '\0\xFF\xD8\xFF': // JPEG
                        metadata.Type = Type::Texture;
                        metadata.Texture.Format = std::byteswap('JPEG');
                        break;
                    case std::byteswap('KB2'): // Bink Video 2
                        metadata.Type = Type::Video;
                        break;
                    default:
                        switch ((metadata.FourCC &= 0xFFFF) << 16)
                        {
                            case '\0\0\xFF\xFB': // MP3
                                metadata.Type = Type::Audio;
                                break;
                            case std::byteswap('MZ'): // EXE / DLL
                            {
                                read(sizeof(IMAGE_DOS_HEADER));
                                auto const ntOffset = ((IMAGE_DOS_HEADER const*)data.data())->e_lfanew;
                                read(ntOffset + sizeof(IMAGE_NT_HEADERS));
                                metadata.Type = ((IMAGE_NT_HEADERS const*)&data[ntOffset])->FileHeader.Characteristics & IMAGE_FILE_DLL ? Type::DLL : Type::EXE;
                                break;
                            }
                            case std::byteswap('PF'):
                                metadata.Type = Type::PackFile;
                                switch (*(fcc const*)&data[8])
                                {
                                    case fcc::anic: metadata.Type = Type::AnimSequences; break;
                                    case fcc::AMSP: metadata.Type = Type::AudioScript; break;
                                    case fcc::ABNK: metadata.Type = Type::Bank; break;
                                    case fcc::ABIX: metadata.Type = Type::BankIndex; break;
                                    case fcc::bone: metadata.Type = Type::BoneScale; break;
                                    case fcc::CINP: metadata.Type = Type::Cinematic; break;
                                    case fcc::cmpc: metadata.Type = Type::Composite; break;
                                    case fcc::locl: metadata.Type = Type::Config; break;
                                    case fcc::cntc: metadata.Type = Type::Content; break;
                                    case fcc::prlt: metadata.Type = Type::ContentPortalManifest; break;
                                    case fcc::DEPS: metadata.Type = Type::DependencyTable; break;
                                    case fcc::emoc: metadata.Type = Type::EmoteAnimation; break;
                                    case fcc::eula: metadata.Type = Type::EULA; break;
                                    case fcc::AFNT: metadata.Type = Type::Font; break;
                                    case fcc::ARMF: metadata.Type = Type::Manifest; break;
                                    case fcc::hvkC: metadata.Type = Type::MapCollision; break;
                                    case fcc::STAR: metadata.Type = Type::MapEnvironment; break;
                                    case fcc::mMet: metadata.Type = Type::MapMetadata; break;
                                    case fcc::mapc: metadata.Type = Type::MapParam; break;
                                    case fcc::mpsd: metadata.Type = Type::MapShadow; break;
                                    case fcc::AMAT: metadata.Type = Type::Material; break;
                                    case fcc::MODL: metadata.Type = Type::Model; break;
                                    case fcc::cmaC: metadata.Type = Type::ModelCollisionManifest; break;
                                    case fcc::PIMG: metadata.Type = Type::PagedImageTable; break;
                                    case fcc::ASND: metadata.Type = Type::Sound; break;
                                    case fcc::CDHS: metadata.Type = Type::ShaderCache; break;
                                    case fcc::txtm: metadata.Type = Type::TextPackManifest; break;
                                    case fcc::txtV: metadata.Type = Type::TextPackVariant; break;
                                    case fcc::txtv: metadata.Type = Type::TextPackVoices; break;
                                    default:
                                        metadata.PackFile.Signature = *(uint32 const*)&data[8];
                                        metadata.PackFile.FirstChunk = *(uint32 const*)&data[12];
                                        break;
                                }
                                break;
                            default:
                                if (std::ranges::all_of(data | std::views::take(readBytes), [](char c) { return std::isprint(c) || std::isspace(c); }))
                                {
                                    metadata.Type = Type::Text;
                                    metadata.FourCC = 0;
                                }
                                else
                                    metadata.FourCC = fccBackup;
                                break;
                        }
                        break;
                }
                break;
        }

        cache.MetadataIndex = AddMetadata(metadata);
        return true;
    }
    catch (...)
    {
        cache.MetadataIndex = AddMetadata({ .Type = Type::Error });
        return false;
    }
}

void ArchiveIndex::OnLoaded() const
{
    if (m_header->Version < CacheHeader::CurrentVersion)
        m_header->Version = CacheHeader::CurrentVersion;
}

}
