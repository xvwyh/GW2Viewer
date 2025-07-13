module;
#include "Utils/Async.h"
#include <mio/mmap.hpp>
#include <cassert>
#include <magic_enum/magic_enum.hpp>

export module GW2Viewer.User.ArchiveIndex;
import GW2Viewer.Common;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Game;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.Visitor;
import std;
import magic_enum;

export namespace GW2Viewer::User
{

struct ArchiveIndex
{
    enum class Type : uint16
    {
        Unscanned,
        Unknown,
        Error,
        Uncategorized,

        EXE,
        DLL,

        Audio,
        Text,
        Texture,
        Video,

        StringFile,
        FontFile,

        PackFile,
        AnimSequences,
        AudioScript,
        Bank,
        BankIndex,
        BoneScale,
        Cinematic,
        Composite,
        Config,
        Content,
        ContentPortalManifest,
        DependencyTable,
        EmoteAnimation,
        EULA,
        Font,
        Manifest,
        MapCollision,
        MapEnvironment,
        MapMetadata,
        MapParam,
        MapShadow,
        Material,
        Model,
        ModelCollisionManifest,
        PagedImageTable,
        Sound,
        ShaderCache,
        TextPackManifest,
        TextPackVariant,
        TextPackVoices,
    };

#pragma pack(push, 1)
    struct CacheHeader
    {
        static constexpr byte CurrentVersion = 1;

        uint32 FourCC = std::byteswap('GW2V');
        uint32 FourCC2 = std::byteswap('AIDX');
        byte Version = CurrentVersion;
        byte ArchiveKind = 0;
        byte ArchiveVersion = 0;
        byte Reserved0 = 0;
        uint32 NumMetadata = 0;
        uint32 NumTimestamps = 0;
        uint32 NumFiles = 0;
        uint64 ArchiveTimestampOnLastRun = 0;
        uint64 ArchiveTimestampOnLastFullScan = 0;
        byte Reserved[0x1000 - 0x28] { };
    };
    static_assert(sizeof(CacheHeader) == 0x1000);
    struct CacheMetadata
    {
        static constexpr byte CurrentVersion = 1;

        byte Version = CurrentVersion;
        byte Reserved0 = 0;
        Type Type = Type::Unscanned;

        uint32 FourCC { };
        union
        {
            byte Raw[0x40 - 0x8] { };
            struct
            {
                uint32 Format;
                uint16 Width;
                uint16 Height;

                auto ToString() const { return std::format("{} - {} x {}", Utils::Format::PrintableFourCC(Format), Width, Height); }
            } Texture;
            struct
            {
                uint32 Signature;
                uint32 FirstChunk;

                auto ToString() const { return std::format("{} - {}", Utils::Format::PrintableFourCC(Signature), Utils::Format::PrintableFourCC(FirstChunk)); }
            } PackFile;
        };

        auto FourCCToString() const { return std::format("{}", Utils::Format::PrintableFourCC(FourCC)); }
        auto DataToString() const
        {
#define FOR(type) [this](magic_enum::enum_constant<Type::type>) { return type.ToString(); }
            return magic_enum::enum_switch(Utils::Visitor::Overloaded
            {
                FOR(Texture),
                FOR(PackFile),
            }, Type);
#undef FOR
        }
        auto ToString() const { return std::format("{}/{} ({})", Utils::Format::PrintableFourCC(FourCC), magic_enum::enum_name(Type), DataToString()); }

        bool operator==(CacheMetadata const& other) const { return !memcmp(this, &other, sizeof(*this)); }
    };
    static_assert(sizeof(CacheMetadata) == 0x40);
    struct CacheTimestamp
    {
        static constexpr byte CurrentVersion = 1;

        byte Version = CurrentVersion;
        byte Reserved0 = 0;
        byte Reserved1 = 0;
        byte Reserved2 = 0;
        uint32 Build = 0;
        uint64 Timestamp = 0;

        bool operator==(CacheTimestamp const& other) const = default;
    };
    static_assert(sizeof(CacheTimestamp) == 0x10);
    struct CacheFile
    {
        static constexpr byte CurrentVersion = 1;

        byte Version = CurrentVersion; // 0 means cache is absent
        byte Deleted : 1 = 0;
        uint16 MetadataIndex = 0;
        uint32 AddedTimestampIndex : 12 = 0;
        uint32 ChangedTimestampIndex : 12 = 0;
        uint32 Reserved1 : 8 = 0;
        uint32 RawFileSize = 0;
        uint32 FileSize = 0;
        uint32 MFTCRC = 0;
        uint32 CombinedBlockCRC = 0;
    };
    static_assert(sizeof(CacheFile) == 0x18);
    struct CacheIndex
    {
        CacheHeader Header;
        CacheMetadata Metadata[0x10000];
        CacheTimestamp Timestamps[0x1000];
        CacheFile Files[];
    };
    static_assert(sizeof(CacheIndex) == sizeof(CacheIndex::Header) + sizeof(CacheIndex::Metadata) + sizeof(CacheIndex::Timestamps));
#pragma pack(pop)

    Utils::Async::Scheduler AsyncScan;

    bool IsLoaded() const { return m_mappedFile.is_mapped() && m_index && m_header; }
    void Load(Data::Archive::Kind kind, std::filesystem::path const& path)
    {
        m_kind = kind;
        m_archiveSource = G::Game.Archive.GetSource(m_kind);
        assert(m_archiveSource);

        bool creating = false;
        if (!exists(path))
        {
            creating = true;
            if (!path.parent_path().empty())
                create_directories(path.parent_path());
            std::ofstream create(path);
        }

        if (uint32 const fileSize = sizeof(CacheIndex) + sizeof(*CacheIndex::Files) * (m_archiveSource->Archive.MaxFileID + 1); file_size(path) < fileSize)
            resize_file(path, fileSize);

        std::error_code error;
        m_mappedFile.map(path.wstring(), 0, 0, error);
        if (error)
            std::terminate();

        m_index = (CacheIndex*)m_mappedFile.data();
        m_header = &m_index->Header;

        if (creating)
        {
            *m_header =
            {
                .ArchiveKind = (byte)m_kind,
                .ArchiveVersion = (byte)(m_archiveSource->Archive.Header.Signature - Data::Archive::Archive::ARCHIVE_SIGNATURE_BASE),
            };
        }

        assert(m_header->FourCC == CacheHeader().FourCC && m_header->FourCC2 == CacheHeader().FourCC2);
        assert(m_header->ArchiveKind == (byte)m_kind);

        m_header->ArchiveTimestampOnLastRun = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(last_write_time(m_archiveSource->Path)).time_since_epoch()).count();

        if (!m_header->NumMetadata)
        {
            AddMetadata({ });
            AddMetadata({ .Type = Type::Unknown });
            AddMetadata({ .Type = Type::Error });
            AddMetadata({ .Type = Type::Uncategorized });
            AddTimestamp({ });
        }
        else
        {
            m_index->Metadata[0].Version = CacheMetadata::CurrentVersion;
            m_index->Metadata[1].Version = CacheMetadata::CurrentVersion;
            m_index->Metadata[2].Version = CacheMetadata::CurrentVersion;
            m_index->Metadata[4].Version = CacheMetadata::CurrentVersion;
            m_index->Timestamps[0].Version = CacheTimestamp::CurrentVersion;
        }
    }
    void Save()
    {
        if (!IsLoaded())
            return;

        std::error_code error;
        m_mappedFile.sync(error);
        if (error)
            std::terminate();
    }

    Data::Archive::Source& GetSource() const
    {
        assert(IsLoaded());
        return *m_archiveSource;
    }
    time_t GetArchiveTimestamp() const
    {
        assert(IsLoaded());
        return m_header->ArchiveTimestampOnLastRun;
    }

    uint16 AddMetadata(CacheMetadata const& metadata)
    {
        assert(IsLoaded());
        assert(m_mappedFile.size() >= sizeof(CacheIndex));
        auto& count = m_header->NumMetadata;
        auto& container = m_index->Metadata;
        auto const end = &container[count];
        auto const itr = std::find(container, end, metadata);
        if (itr == end)
        {
            assert(count < std::size(container));
            *itr = metadata;
            ++count;
        }
        return std::distance(container, itr);
    }
    CacheMetadata const& GetMetadata(uint16 index) const
    {
        assert(IsLoaded());
        assert(m_mappedFile.size() >= sizeof(CacheIndex));
        assert(index < m_header->NumMetadata);
        return m_index->Metadata[index];
    }
    CacheMetadata const& GetFileMetadata(uint32 fileID) const { return GetMetadata(GetFile(fileID).MetadataIndex); }

    uint16 AddTimestamp() { return AddTimestamp({ .Build = G::Game.Build, .Timestamp = m_header->ArchiveTimestampOnLastRun }); }
    uint16 AddTimestamp(CacheTimestamp const& timestamp)
    {
        assert(IsLoaded());
        assert(m_mappedFile.size() >= sizeof(CacheIndex));
        auto& count = m_header->NumTimestamps;
        auto& container = m_index->Timestamps;
        auto const end = &container[count];
        auto const itr = std::find(container, end, timestamp);
        if (itr == end)
        {
            assert(count < std::size(container));
            *itr = timestamp;
            ++count;
        }
        return std::distance(container, itr);
    }
    CacheTimestamp const& GetTimestamp(uint16 index) const
    {
        assert(IsLoaded());
        assert(m_mappedFile.size() >= sizeof(CacheIndex));
        assert(index < m_header->NumTimestamps);
        return m_index->Timestamps[index];
    }
    CacheTimestamp const& GetFileAddedTimestamp(uint32 fileID) const { return GetTimestamp(GetFile(fileID).AddedTimestampIndex); }
    CacheTimestamp const& GetFileChangedTimestamp(uint32 fileID) const { return GetTimestamp(GetFile(fileID).ChangedTimestampIndex); }

    CacheFile const& GetFile(uint32 fileID) const
    {
        assert(IsLoaded());
        assert(m_mappedFile.size() >= sizeof(CacheIndex) + sizeof(*CacheIndex::Files) * (fileID + 1));
        return m_index->Files[fileID];
    }

    enum class CheckCacheResult
    {
        NoFile,
        CacheMissing,
        CacheValid,
        CacheValidOutdated,
        CacheCorrupted,
        FileMissing,
        FileChangedMFTCRC,
        FileChangedRawFileSize,
        FileChangedFileSize,
        FileChangedCombinedBlockCRC,
    };
    CheckCacheResult CheckCache(CacheFile const& cache, uint32 fileID, uint32* outCombinedBlockCRC) const
    {
        auto const entry = m_archiveSource->Archive.GetFileMftEntry(fileID);

        if (!cache.Version || !cache.MetadataIndex || !cache.RawFileSize)
            return entry ? CheckCacheResult::CacheMissing : CheckCacheResult::NoFile;

        if (cache.Version > CacheFile::CurrentVersion || cache.MetadataIndex >= m_header->NumMetadata || cache.AddedTimestampIndex >= m_header->NumTimestamps || cache.ChangedTimestampIndex >= m_header->NumTimestamps)
            return CheckCacheResult::CacheCorrupted;

        if (!entry)
            return CheckCacheResult::FileMissing;

        if (cache.MFTCRC != entry->alloc.crc)
            return CheckCacheResult::FileChangedMFTCRC;

        if (cache.RawFileSize != entry->alloc.size)
            return CheckCacheResult::FileChangedRawFileSize;

        if (cache.FileSize != m_archiveSource->Archive.GetFileSize(fileID))
            return CheckCacheResult::FileChangedFileSize;

        uint32 const combinedBlockCRC = m_archiveSource->Archive.CalculateRawFileCRC(fileID);
        if (outCombinedBlockCRC)
            *outCombinedBlockCRC = combinedBlockCRC;
        if (cache.CombinedBlockCRC != combinedBlockCRC)
            return CheckCacheResult::FileChangedCombinedBlockCRC;

        if (cache.Version != CacheFile::CurrentVersion)
            return CheckCacheResult::CacheValidOutdated;
        return CheckCacheResult::CacheValid;
    }
    bool UpdateCache(CacheFile& cache, uint32 fileID, uint32 const* precalculatedCombinedBlockCRC);
    struct ScanOptions
    {
        std::optional<std::vector<uint32>> Files;
        bool UpdateUnknown = true;
        bool UpdateError = true;
        bool UpdateUncategorized = true;
        bool UpdateCategorized = false;

        bool ShouldUpdate(Type type) const
        {
            switch (type)
            {
                case Type::Unscanned:     return true;
                case Type::Unknown:       return UpdateUnknown;
                case Type::Error:         return UpdateError;
                case Type::Uncategorized: return UpdateUncategorized;
                default:                  return UpdateCategorized;
            }
        }
    };
    struct ScanProgress
    {
        uint32 NewFiles = 0;
        uint32 ChangedFiles = 0;
        uint32 DeletedFiles = 0;
        uint32 ErrorFiles = 0;
        uint32 CorruptedCaches = 0;
    };
    struct ScanResult
    {
        uint32 Updated = 0;
        uint32 Scanned = 0;
        uint32 Total = 0;
        std::vector<uint32> NewFiles;
        std::map<uint32, CacheFile> ChangedFiles;
        std::map<uint32, CacheFile> DeletedFiles;
        std::vector<uint32> ErrorFiles;
        std::map<uint32, CacheFile> CorruptedCaches;
    };
    void ScanAsync(ScanOptions options, ScanProgress& progress, std::function<void(ScanResult const&)> callback)
    {
        assert(IsLoaded());
        AsyncScan.Run([this, &progress, options = std::move(options), callback = std::move(callback)](Utils::Async::Context context) { callback(Scan(options, progress, context)); });
    }
    void StopScan()
    {
        AsyncScan.Run([](Utils::Async::Context context) { context->Finish(); });
    }

private:
    Data::Archive::Kind m_kind { };
    Data::Archive::Source* m_archiveSource = nullptr;
    mio::mmap_sink m_mappedFile { };
    CacheIndex* m_index = nullptr;
    CacheHeader* m_header = nullptr;

    ScanResult Scan(ScanOptions const& options, ScanProgress& progress, Utils::Async::Context context)
    {
        ScanResult result;
        auto process = [this, &options, &progress, &result](uint32 fileID, CacheFile& cache)
        {
            if (!m_archiveSource->Archive.GetFileMftEntry(fileID))
                return;

            ++result.Scanned;
            uint32 combinedBlockCRC = 0;
            bool hasCRC = false;
            bool fileChanged = false;
            switch (CheckCache(cache, fileID, &combinedBlockCRC))
            {
                case CheckCacheResult::CacheValidOutdated:
                    cache.Version = CacheFile::CurrentVersion;
                    [[fallthrough]];
                case CheckCacheResult::CacheValid:
                case CheckCacheResult::NoFile:
                    if (options.UpdateCategorized)
                        break;
                    return;

                case CheckCacheResult::FileMissing:
                    ++progress.DeletedFiles;
                    result.DeletedFiles.emplace(fileID, cache);
                    cache.Deleted = true;
                    cache.ChangedTimestampIndex = AddTimestamp();
                    return;

                case CheckCacheResult::CacheMissing:
                    ++progress.NewFiles;
                    result.NewFiles.emplace_back(fileID);
                    cache.AddedTimestampIndex = AddTimestamp();
                    break;
                case CheckCacheResult::CacheCorrupted:
                    ++progress.CorruptedCaches;
                    result.CorruptedCaches.emplace(fileID, cache);
                    break;
                case CheckCacheResult::FileChangedCombinedBlockCRC:
                    hasCRC = true;
                    [[fallthrough]];
                case CheckCacheResult::FileChangedMFTCRC:
                case CheckCacheResult::FileChangedRawFileSize:
                case CheckCacheResult::FileChangedFileSize:
                    ++progress.ChangedFiles;
                    result.ChangedFiles.emplace(fileID, cache);
                    cache.ChangedTimestampIndex = AddTimestamp();
                    fileChanged = true;
                    break;
            }
            if (fileChanged || options.ShouldUpdate(GetMetadata(cache.MetadataIndex).Type))
            {
                ++result.Updated;
                if (!UpdateCache(cache, fileID, hasCRC ? &combinedBlockCRC : nullptr))
                {
                    ++progress.ErrorFiles;
                    result.ErrorFiles.emplace_back(fileID);
                }
            }
        };

        if (options.Files)
        {
            if (options.Files->empty())
                return result;

            [&]
            {
                context->SetTotal(result.Total = options.Files->size());
                for (auto const fileID : *options.Files)
                {
                    CHECK_ASYNC;
                    assert(m_mappedFile.size() >= sizeof(CacheIndex) + sizeof(*CacheIndex::Files) * (fileID + 1));
                    process(fileID, m_index->Files[fileID]);
                    context->Increment();
                }
            }();
        }
        else
        {
            [&]
            {
                uint32 const numFiles = std::max(m_header->NumFiles, m_archiveSource->Archive.MaxFileID + 1);
                context->SetTotal(result.Total = numFiles);
                for (auto&& [fileID, cache] : std::span { m_index->Files, numFiles } | std::views::enumerate)
                {
                    CHECK_ASYNC;
                    process(fileID, cache);
                    context->Increment();
                }
                m_header->ArchiveTimestampOnLastFullScan = m_header->ArchiveTimestampOnLastRun;
            }();
        }

        Save();
        if (context)
            context->Finish();
        return result;
    }
};

}

export namespace GW2Viewer::G
{
    magic_enum::containers::array<Data::Archive::Kind, User::ArchiveIndex> ArchiveIndex;
}
