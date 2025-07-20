module;
#include <cassert>

export module GW2Viewer.Data.Archive;
import GW2Viewer.Common;
import GW2Viewer.Data.Manifest.Asset;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.Utils.Async.ProgressBarContext;
import GW2Viewer.Utils.CRC;
import std;
import <gw2dattools/compression/inflateDatFileBuffer.h>;
import <boost/container/small_vector.hpp>;

export namespace GW2Viewer::Data::Archive
{

class Archive
{
public:
    enum
    {
        IndexMFTHeader = 0,
        IndexArchiveHeader = 1,
        IndexDirectory = 2,
        INDEX_MFT = 3,
        INDEX_FIRST_FILE = 16,

        FILE_ID_UNUSED = 0,

        FLAG_ENTRY_USED = 1,
        FLAG_FIRST_STREAM = 2,

        ARCHIVE_SIGNATURE_BASE = '\x1ANA2',
        ARCHIVE_VERSION = 101,
    };

#pragma pack(push, 1)
    struct ArchiveHeaderV0
    {
        uint32 Signature = ARCHIVE_SIGNATURE_BASE + ARCHIVE_VERSION;
        uint32 BlockSize = 0x200;
        uint64 MFTOffset;
        uint32 MFTSize;
        uint32 Flags;
    };
    struct ArchiveHeaderV1
    {
        uint32 Signature = ARCHIVE_SIGNATURE_BASE + ARCHIVE_VERSION;
        uint32 HeaderSize = sizeof(ArchiveHeaderV1);
        uint32 HeaderVersion = 0xCABA0001;
        uint32 BlockSize = 0x200;
        uint32 CRC /*= CRC::Calculate(0, this, offsetof(ArchiveHeaderV1, CRC))*/;
        uint32 unknownField2;
        uint64 MFTOffset;
        uint32 MFTSize;
        uint32 Flags;
    };
    using ArchiveHeader = ArchiveHeaderV1;

    struct MftEntry
    {
        union
        {
            struct
            {
                byte signature[4];
                uint32 writes;
                uint32 unknownField1;
                uint32 numEntries;              /**< Amount of entries in the MFT, including this. */
            } descriptor;
            struct
            {
                uint64 offset;
                uint32 size;
                uint16 extraBytes;
                byte flags;
                byte stream;
                uint32 nextStream;
                uint32 crc;
            } alloc;
        };
    };

    struct DirectoryEntry
    {
        uint32 fileId;
        uint32 mftIndex;
    };
    /* Inside Directory:
    #pragma pack(push, 8)
    struct FileHash
    {
        FileHash const* prevInMft;
        FileHash const* Prev;
        FileHash const* Next;
        uint32 FileID;
        uint32 DirectoryIndex;
        uint32 MftIndex;
    };
    struct Mft
    {
        FileHash const* head;
        uint32 refCount;
    };
    #pragma pack(pop)
    std::vector<Mft> m_mftArray;
    */
#pragma pack(pop)

    ArchiveHeader Header;
    std::vector<MftEntry> m_entryArray;
    std::vector<DirectoryEntry> DirectoryEntries;
    std::vector<Manifest::Asset> ManifestAssets;
    std::map<uint32, std::tuple<MftEntry*, DirectoryEntry*, Manifest::Asset*>> FileLookup;
    std::multimap<uint32, uint32> MftIndexToFileId;
    uint32 MaxFileID = 0;

    bool Open(std::filesystem::path const& path, Utils::Async::ProgressBarContext& progress)
    {
        std::string const prefix = std::format("Loading {}: ", path.filename().string());

        progress.Start(prefix + "Opening");
        m_file = std::ifstream(path, std::ios::binary);
        if (!m_file)
            return false;

        progress.Start(prefix + "Reading header");
        Read(Header);
        m_entryArray.resize(1);
        Read(m_entryArray.front(), Header.MFTOffset);

        progress.Start(prefix + "Reading MFT");
        auto const& descriptor = m_entryArray[IndexMFTHeader];
        assert(Header.MFTSize == descriptor.descriptor.numEntries * sizeof(MftEntry));
        m_entryArray.resize(descriptor.descriptor.numEntries);
        Read(m_entryArray.front(), Header.MFTOffset, Header.MFTSize);

        progress.Start(prefix + "Reading file ID database");
        auto& fileMap = m_entryArray[IndexDirectory];
        assert(!(fileMap.alloc.size % sizeof(DirectoryEntry)));
        DirectoryEntries.resize(fileMap.alloc.size / sizeof(DirectoryEntry));
        Read(DirectoryEntries.front(), fileMap.alloc.offset, fileMap.alloc.size);

        progress.Start(prefix + "Assembling file lookup table", DirectoryEntries.size());
        ManifestAssets.resize(m_entryArray.size());
        for (auto const& [index, entry] : DirectoryEntries | std::views::enumerate)
        {
            if (!entry.mftIndex)
                continue;

            auto [itr, added] = FileLookup.try_emplace(entry.fileId, &m_entryArray[entry.mftIndex], &entry, &ManifestAssets[entry.mftIndex]);
            assert(added);
            //MftIndexToFileId.emplace(entry.mftIndex, entry.fileId);
            if (MaxFileID < entry.fileId)
                MaxFileID = entry.fileId;

            if (!(index % 1000))
                progress = index;
        }
        return true;
    }

    [[nodiscard]] auto GetFileIDs() const { return FileLookup | std::ranges::views::keys; }
    MftEntry const* GetFileMftEntry(uint32 fileID) const
    {
        auto const itr = FileLookup.find(fileID);
        return itr != FileLookup.end() ? std::get<MftEntry*>(itr->second) : nullptr;
    }
    DirectoryEntry const* GetFileDirectoryEntry(uint32 fileID) const
    {
        auto const itr = FileLookup.find(fileID);
        return itr != FileLookup.end() ? std::get<DirectoryEntry*>(itr->second) : nullptr;
    }
    Manifest::Asset* GetFileManifestAsset(uint32 fileID) const
    {
        auto const itr = FileLookup.find(fileID);
        return itr != FileLookup.end() ? std::get<Manifest::Asset*>(itr->second) : nullptr;
    }
    uint32 GetRawFileSize(uint32 fileID) const
    {
        if (auto entryPtr = GetFileMftEntry(fileID))
            if (MftEntry const& entry = *entryPtr; entry.alloc.flags & FLAG_ENTRY_USED)
                return entry.alloc.size;
        return 0;
    }
    std::vector<byte> GetRawFile(uint32 fileID)
    {
        std::vector<byte> result(GetRawFileSize(fileID));
        GetRawFile(fileID, result);
        return result;
    }
    void GetRawFile(uint32 fileID, std::span<byte> buffer)
    {
        if (auto entryPtr = GetFileMftEntry(fileID))
            if (MftEntry const& entry = *entryPtr; entry.alloc.flags & FLAG_ENTRY_USED)
                Read(buffer.front(), entry.alloc.offset, entry.alloc.size);
    }
    uint32 CalculateRawFileCRC(uint32 fileID)
    {
        uint32 crc = 0;
        if (auto entryPtr = GetFileMftEntry(fileID))
        {
            if (MftEntry const& entry = *entryPtr; entry.alloc.flags & FLAG_ENTRY_USED)
            {
                uint32 const blocks = (entry.alloc.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
                for (uint32 i = 0; i < blocks; ++i)
                {
                    uint32 blockCRC = 0;
                    Read(blockCRC, entry.alloc.offset + i * BLOCK_SIZE + std::min<size_t>(BLOCK_DATA_SIZE, entry.alloc.size - i * BLOCK_SIZE - BLOCK_CRC_SIZE) - BLOCK_CRC_SIZE);
                    crc = Utils::CRC::Calculate(crc, { (byte const*)&blockCRC, sizeof(blockCRC) });
                }
            }
        }
        return crc;
    }
    uint32 GetFileSize(uint32 fileID)
    {
        if (auto entryPtr = GetFileMftEntry(fileID))
        {
            if (MftEntry const& entry = *entryPtr; entry.alloc.flags & FLAG_ENTRY_USED)
            {
                if (!entry.alloc.extraBytes)
                    return entry.alloc.size - (entry.alloc.size + BLOCK_SIZE - 1) / BLOCK_SIZE * sizeof(uint32);

                enum class Type : uint16
                {
                    Invalid = 0x8000,
                    UncompressedSize,
                    Unknown,
                };
                struct Header
                {
                    uint16 Size;
                    Type Type;
                };

                uint32 uncompressedSize = 0;
                boost::container::small_vector<byte, 16> headers((entry.alloc.extraBytes + 15) / 16 * 16);
                Read(*headers.data(), entry.alloc.offset, headers.size());
                byte* p = headers.data();
                while (std::distance(headers.data(), p) + sizeof(Header) <= entry.alloc.extraBytes)
                {
                    auto header = (Header*)p;
                    switch (header->Type)
                    {
                        case Type::UncompressedSize:
                            struct UncompressedSize : Header
                            {
                                uint32 Value;
                            };
                            if (header->Size != sizeof(UncompressedSize))
                                return 0;
                            uncompressedSize = ((UncompressedSize*&)p)++->Value;
                            break;
                        case Type::Unknown:
                            struct Unknown : Header
                            {
                                uint32 Value;
                            };
                            if (header->Size != sizeof(Unknown))
                                return 0;
                            ((Unknown*&)p)++;
                            break;
                        default:
                            if (header->Type >= Type::Invalid)
                                return 0;
                            p += header->Size * 2;
                            break;
                    }
                }
                if (p == headers.data() + entry.alloc.extraBytes)
                    return uncompressedSize;
            }
        }
        return 0;
    }
    std::vector<byte> GetFile(uint32 fileID)
    {
        std::vector<byte> result(GetFileSize(fileID));
        GetFile(fileID, result);
        return result;
    }
    uint32 GetFile(uint32 fileID, std::span<byte> buffer, bool partial = false)
    {
        if (auto entryPtr = GetFileMftEntry(fileID))
        {
            if (MftEntry const& entry = *entryPtr; entry.alloc.flags & FLAG_ENTRY_USED)
            {
                if (entry.alloc.extraBytes)
                {
                    uint32 size = buffer.size();
                    boost::container::small_vector<byte, 0x200> compressed(partial ? std::min(std::max(0x200u, size), entry.alloc.size) : entry.alloc.size);
                    Read(compressed.front(), entry.alloc.offset, compressed.size());
                    gw2dt::compression::inflateDatFileBuffer(compressed.size(), compressed.data(), size, buffer.data());
                    assert(size == buffer.size());
                    return size;
                }
                else
                {
                    byte* p = buffer.data();
                    uint32 const blocks = (std::min<uint32>(entry.alloc.size, buffer.size()) + BLOCK_SIZE - 1) / BLOCK_SIZE;
                    for (uint32 i = 0; i < blocks; ++i)
                    {
                        uint32 const readSize = std::min<size_t>(std::min(BLOCK_DATA_SIZE, entry.alloc.size - i * BLOCK_SIZE - BLOCK_CRC_SIZE), std::distance(p, buffer.data() + buffer.size()));
                        Read(*p, entry.alloc.offset + i * BLOCK_SIZE, readSize);
                        p += readSize;
                    }
                    return std::distance(buffer.data(), p);
                }
            }
        }
        return 0;
    }
    std::unique_ptr<Pack::PackFile> GetPackFile(uint32 fileID)
    {
        std::unique_ptr<Pack::PackFile> result;
        if (auto size = GetFileSize(fileID))
        {
            result.reset(Pack::PackFile::Alloc(size));
            GetFile(fileID, { (byte*)result.get(), size });
        }
        return result;
    }

private:
    static constexpr uint32 BLOCK_SIZE = 0x10000;
    static constexpr uint32 BLOCK_CRC_SIZE = sizeof(uint32);
    static constexpr uint32 BLOCK_DATA_SIZE = BLOCK_SIZE - BLOCK_CRC_SIZE;

    std::ifstream m_file;
    std::mutex m_mutex;

    template<typename T>
    void Read(T& target, std::optional<std::streampos> pos = std::nullopt, std::streamsize size = sizeof(T))
    {
        std::scoped_lock _(m_mutex);
        if (pos)
            m_file.seekg(*pos, std::ios::beg);
        if (!m_file)
            std::terminate();
        m_file.read((char*)&target, size);
        if (!m_file)
            std::terminate();
    }
};

enum class Kind
{
    Game,
    Local,
};
struct File;
struct Source
{
    uint32 LoadOrder;
    Kind Kind;
    std::filesystem::path Path;
    Archive Archive;
    std::vector<File> Files;
    std::unordered_map<uint32, std::reference_wrapper<File const>> FileLookup;

    File const* GetFile(uint32 fileID) const
    {
        auto itr = FileLookup.find(fileID);
        return itr != FileLookup.end() ? &itr->second.get() : nullptr;
    }

    auto operator<=>(Source const& other) const { return LoadOrder <=> other.LoadOrder; }
};
struct File
{
    uint32 ID;

    File(uint32 fileID, Source& source, Archive::MftEntry const& mft, Archive::DirectoryEntry const& directory, Manifest::Asset& asset) : ID(fileID), m_source(source), m_mft(mft), m_directory(directory), m_asset(asset) { }

    auto& GetSource() const { return m_source.get(); }
    auto GetSourceLoadOrder() const { return GetSource().LoadOrder; }
    auto GetSourceKind() const { return GetSource().Kind; }
    auto& GetSourcePath() const { return GetSource().Path; }
    auto& GetArchive() const { return GetSource().Archive; }

    auto& GetMftEntry() const { return m_mft.get(); }
    auto& GetDirectoryEntry() const { return m_directory.get(); }
    auto& GetManifestAsset() const { return m_asset.get(); }

    auto GetRawSize() const { return GetArchive().GetRawFileSize(ID); }
    auto GetRawData() const { return GetArchive().GetRawFile(ID); }
    auto GetRawData(std::span<byte> buffer) const { return GetArchive().GetRawFile(ID, buffer); }

    auto GetSize() const { return GetArchive().GetFileSize(ID); }
    auto GetData() const { return GetArchive().GetFile(ID); }
    auto GetData(std::span<byte> buffer) const { return GetArchive().GetFile(ID, buffer); }

    auto GetPackFile() const { return GetArchive().GetPackFile(ID); }

    auto& GetBestVersion() const
    {
        if (auto const streamBaseID = GetManifestAsset().StreamBaseID)
            return *GetSource().GetFile(streamBaseID);
        return *this;
    }

    std::strong_ordering operator<=>(File const& other) const
    {
        if (auto const result = ID <=> other.ID; result != std::strong_ordering::equal) return result;
        if (auto const result = GetSource() <=> other.GetSource(); result != std::strong_ordering::equal) return result;
        return std::strong_ordering::equal;
    }
    bool operator==(File const& other) const { return *this <=> other == std::strong_ordering::equal; }

private:
    std::reference_wrapper<Source> m_source;
    std::reference_wrapper<Archive::MftEntry const> m_mft;
    std::reference_wrapper<Archive::DirectoryEntry const> m_directory;
    std::reference_wrapper<Manifest::Asset> m_asset;
};

}
