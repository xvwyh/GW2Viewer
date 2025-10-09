module;
#include <magic_enum/magic_enum.hpp>
#include <mio/mmap.hpp>
#include <cassert>

export module GW2Viewer.User.ArchiveIndex;
import GW2Viewer.Common;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Manifest.Asset;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.Visitor;
import std;
import magic_enum;
#include "Macros.h"

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
        static constexpr byte CurrentVersion = 2;

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
        static constexpr byte CurrentVersion = 2;

        byte Version = CurrentVersion; // 0 means cache is absent
        byte Deleted : 1 = 0;
        byte IsRevision : 1 = 0;
        byte IsStream : 1 = 0;
        uint16 MetadataIndex = 0;
        uint32 AddedTimestampIndex : 12 = 0;
        uint32 ChangedTimestampIndex : 12 = 0;
        uint32 Reserved1 : 8 = 0;
        uint32 RawFileSize = 0;
        uint32 FileSize = 0;
        uint32 BaseOrFileID = 0;
        uint32 ParentOrStreamBaseID = 0;

        uint32 GetBaseID() const { return Version >= 2 ? IsRevision ? BaseOrFileID : 0 : 0; }
        uint32 GetFileID() const { return Version >= 2 ? IsRevision ? 0 : BaseOrFileID : 0; }
        uint32 GetParentBaseID() const { return Version >= 2 ? IsStream ? ParentOrStreamBaseID : 0 : 0; }
        uint32 GetStreamBaseID() const { return Version >= 2 ? IsStream ? 0 : ParentOrStreamBaseID : 0; }
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
    void Load(Data::Archive::Source& source, std::filesystem::path const& path)
    {
        m_kind = source.Kind;
        m_archiveSource = &source;
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
        m_mappedFile.map(path.wstring(), error);
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

        m_header->ArchiveTimestampOnLastRun = Time::ToTimestamp(last_write_time(m_archiveSource->Path));

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

        // Add a timestamp for the current game build even without scanning for file changes,
        // as this can double as a database of game builds and their approximate release dates
        if (G::Game.Build)
            AddTimestamp();

        OnLoaded();
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

    uint16 AddMetadata(CacheMetadata const& metadata) const
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

    uint16 AddTimestamp() const { return AddTimestamp({ .Build = G::Game.Build, .Timestamp = m_header->ArchiveTimestampOnLastRun }); }
    uint16 AddTimestamp(CacheTimestamp const& timestamp) const
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
        auto& cache = m_index->Files[fileID];
        if (cache.Version && cache.Version < CacheFile::CurrentVersion)
            UpdateCacheVersion(cache, fileID);
        return cache;
    }

    enum class CheckCacheResult
    {
        NoFile,
        CacheMissing,
        CacheValid,
        CacheValidOutdated,
        CacheCorrupted,
        FileMissing,
        FileChanged,
    };
    CheckCacheResult CheckCache(CacheFile const& cache, uint32 fileID) const
    {
        auto const entry = m_archiveSource->Archive.GetFileMftEntry(fileID);

        if (!cache.Version || !cache.MetadataIndex || !cache.RawFileSize)
            return entry ? CheckCacheResult::CacheMissing : CheckCacheResult::NoFile;

        if (cache.Version > CacheFile::CurrentVersion || cache.MetadataIndex >= m_header->NumMetadata || cache.AddedTimestampIndex >= m_header->NumTimestamps || cache.ChangedTimestampIndex >= m_header->NumTimestamps)
            return CheckCacheResult::CacheCorrupted;

        if (!entry)
            return cache.Deleted ? CheckCacheResult::CacheValid : CheckCacheResult::FileMissing;

        if (auto const expected = GetExpectedCacheFile(fileID);
            cache.IsRevision != expected.IsRevision ||
            cache.IsStream != expected.IsStream ||
            cache.BaseOrFileID != expected.BaseOrFileID ||
            cache.ParentOrStreamBaseID != expected.ParentOrStreamBaseID)
            return CheckCacheResult::FileChanged;

        if (cache.Version != CacheFile::CurrentVersion)
            return CheckCacheResult::CacheValidOutdated;
        return CheckCacheResult::CacheValid;
    }
    bool UpdateCache(CacheFile& cache, uint32 fileID) const;
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

    void OnLoaded() const;

    ScanResult Scan(ScanOptions const& options, ScanProgress& progress, Utils::Async::Context context)
    {
        ScanResult result;
        auto process = [this, &options, &progress, &result](uint32 fileID, CacheFile& cache)
        {
            ++result.Scanned;
            bool fileChanged = false;
            if (cache.Version && cache.Version < CacheFile::CurrentVersion)
                UpdateCacheVersion(cache, fileID);
            switch (CheckCache(cache, fileID))
            {
                case CheckCacheResult::NoFile:
                    --result.Scanned;
                    return;

                case CheckCacheResult::CacheValidOutdated:
                    cache.Version = CacheFile::CurrentVersion;
                    [[fallthrough]];
                case CheckCacheResult::CacheValid:
                    if (options.UpdateCategorized && !cache.Deleted)
                        break;
                    return;

                case CheckCacheResult::FileMissing:
                    if (!cache.IsRevision)
                    {
                        ++progress.DeletedFiles;
                        result.DeletedFiles.emplace(fileID, cache);
                    }
                    cache.Deleted = true;
                    cache.ChangedTimestampIndex = AddTimestamp();
                    return;

                case CheckCacheResult::CacheMissing:
                    if (!GetExpectedCacheFile(fileID).IsRevision)
                    {
                        ++progress.NewFiles;
                        result.NewFiles.emplace_back(fileID);
                    }
                    cache.AddedTimestampIndex = AddTimestamp();
                    break;
                case CheckCacheResult::CacheCorrupted:
                    ++progress.CorruptedCaches;
                    result.CorruptedCaches.emplace(fileID, cache);
                    break;
                case CheckCacheResult::FileChanged:
                    assert(!cache.IsRevision);
                    ++progress.ChangedFiles;
                    result.ChangedFiles.emplace(fileID, cache);
                    cache.ChangedTimestampIndex = AddTimestamp();
                    fileChanged = true;
                    break;
            }
            if (fileChanged || options.ShouldUpdate(GetMetadata(cache.MetadataIndex).Type))
            {
                ++result.Updated;
                if (!UpdateCache(cache, fileID))
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

    CacheFile GetExpectedCacheFile(uint32 fileID) const
    {
        auto const asset = m_archiveSource->Archive.GetFileManifestAsset(fileID);
        bool const hasRevision = asset && asset->BaseID && asset->FileID && asset->BaseID != asset->FileID;
        bool const hasStream = asset && (asset->ParentBaseID || asset->StreamBaseID);
        return {
            .IsRevision = hasRevision && asset->FileID == fileID,
            .IsStream = hasStream && asset->ParentBaseID,
            .BaseOrFileID = hasRevision ? asset->FileID == fileID ? asset->BaseID : asset->FileID : 0,
            .ParentOrStreamBaseID = hasStream ? asset->ParentBaseID ? asset->ParentBaseID : asset->StreamBaseID : 0,
        };
    }

    void UpdateCacheVersion(CacheFile& cache, uint32 fileID) const
    {
        if (!cache.Version)
            return;

        if (cache.Version < 2)
        {
            auto const expected = GetExpectedCacheFile(fileID);
            cache.IsRevision = expected.IsRevision;
            cache.IsStream = expected.IsStream;
            cache.BaseOrFileID = expected.BaseOrFileID;
            cache.ParentOrStreamBaseID = expected.ParentOrStreamBaseID;
            cache.Version = 2;
        }
    }
};

}

export namespace GW2Viewer::G
{
    magic_enum::containers::array<Data::Archive::Kind, User::ArchiveIndex> ArchiveIndex;
}
