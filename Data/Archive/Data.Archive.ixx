module;
#include <cassert>

export module GW2Viewer.Data.Archive;
import GW2Viewer.Common;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.Utils.ProgressBarContext;
import std;
import <gw2dattools/compression/inflateDatFileBuffer.h>;

export namespace Data::Archive
{

#pragma pack(push, 1)
class Archive
{
public:
    enum
    {
        IndexMFTHeader = 0,
        IndexArchiveHeader = 1,
        IndexFileToMftEntryMap = 2,
        INDEX_MFT = 3,
        INDEX_FIRST_FILE = 16,

        FILE_ID_UNUSED = 0,

        FLAG_ENTRY_USED = 1,
        FLAG_FIRST_STREAM = 2,
    };

    /** Compression flags that appear in the MFT entries. */
    enum ANetCompressionFlags : uint16
    {
        ANCF_Uncompressed = 0,          /**< File is uncompressed. */
        ANCF_Compressed = 8,            /**< File is compressed. */
    };

    struct ArchiveHeader
    {
        byte version;                   /**< Version of the .dat file format. */
        byte identifier[3];             /**< 0x41 0x4e 0x1a */
        uint32 headerSize;              /**< Size of this header. */
        uint32 unknownField1;
        uint32 chunkSize;               /**< Size of each chunk in the file. */
        uint32 cRC;                     /**< CRC of the 16 first bytes of the header. */
        uint32 unknownField2;
        uint64 mftOffset;               /**< Offset to the MFT, from the start of the file. */
        uint32 mftSize;                 /**< Size of the MFT, in bytes. */
        uint32 flags;
    };

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
                uint64 unknownField2;
            } descriptor;
            struct
            {
                uint64 offset;
                uint32 size;
                ANetCompressionFlags compressionFlag;
                byte flags;
                byte stream;
                uint32 nextStream;
                uint32 crc;                     /**< Was 'crc' in GW1, seems to have different usage in GW2. */
            } alloc;
        };
    };

    struct FileIdEntry
    {
        uint32 fileId;                  /**< File ID. */
        uint32 mftEntryIndex;           /**< Index of the file in the mft. */
    };
    /*
    #pragma pack(push, 8)
    struct MftBaseFileEntry
    {
    bool InUse;
    MftFileEntry const* Prev;
    MftFileEntry const* Next;
    uint32 FileID;
    };
    struct MftArrayEntry
    {
    MftBaseFileEntry const* Base;
    uint32 refCount;
    };
    #pragma pack(pop)
    std::vector<MftArrayEntry> m_mftArray;
    */

    ArchiveHeader Header;
    std::vector<MftEntry> MftEntries;
    std::vector<FileIdEntry> FileIdEntries;
    std::map<uint32, MftEntry*> FileIdToMftEntry;
    std::multimap<uint32, uint32> MftEntryIndexToFileId;
    uint32 MaxFileID = 0;

    bool Open(std::filesystem::path const& path, ProgressBarContext& progress)
    {
        std::string const prefix = std::format("Loading {}: ", path.filename().string());

        progress.Start(prefix + "Opening");
        m_file = std::ifstream(path, std::ios::binary);
        if (!m_file)
            return false;

        progress.Start(prefix + "Reading header");
        Read(Header);
        MftEntries.resize(1);
        Read(MftEntries.front(), Header.mftOffset);

        progress.Start(prefix + "Reading MFT");
        auto const& descriptor = MftEntries.front();
        assert(Header.mftSize == descriptor.descriptor.numEntries * sizeof(MftEntry));
        MftEntries.resize(descriptor.descriptor.numEntries);
        Read(MftEntries.front(), Header.mftOffset, Header.mftSize);

        progress.Start(prefix + "Reading file ID database");
        auto& fileMap = MftEntries[IndexFileToMftEntryMap];
        assert(!(fileMap.alloc.size % sizeof(FileIdEntry)));
        FileIdEntries.resize(fileMap.alloc.size / sizeof(FileIdEntry));
        Read(FileIdEntries.front(), fileMap.alloc.offset, fileMap.alloc.size);

        progress.Start(prefix + "Assembling file lookup table", FileIdEntries.size());
        for (auto const& [index, entry] : FileIdEntries | std::views::enumerate)
        {
            if (!entry.mftEntryIndex)
                continue;

            FileIdToMftEntry.try_emplace(entry.fileId, &MftEntries[entry.mftEntryIndex]);
            //MftEntryIndexToFileId.emplace(entry.mftEntryIndex, entry.fileId);
            if (MaxFileID < entry.fileId)
                MaxFileID = entry.fileId;

            if (!(index % 1000))
                progress = index;
        }
        return true;
    }

    [[nodiscard]] auto GetFileIDs() const { return FileIdToMftEntry | std::ranges::views::keys; }
    uint32 GetFileSize(uint32 fileID)
    {
        if (auto const itr = FileIdToMftEntry.find(fileID); itr != FileIdToMftEntry.end())
        {
            if (MftEntry const& entry = *itr->second; entry.alloc.flags & FLAG_ENTRY_USED)
            {
                if (entry.alloc.compressionFlag & ANCF_Compressed)
                {
                    CompressedFileHeader header;
                    Read(header, entry.alloc.offset, sizeof(header));
                    return header.UncompressedSize;
                }
                return entry.alloc.size - (entry.alloc.size + BLOCK_SIZE - 1) / BLOCK_SIZE * sizeof(uint32);
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
    void GetFile(uint32 fileID, std::span<byte> buffer)
    {
        if (auto const itr = FileIdToMftEntry.find(fileID); itr != FileIdToMftEntry.end())
        {
            if (MftEntry const& entry = *itr->second; entry.alloc.flags & FLAG_ENTRY_USED)
            {
                if (entry.alloc.compressionFlag & ANCF_Compressed)
                {
                    std::vector<byte> compressed(entry.alloc.size);
                    Read(*compressed.data(), entry.alloc.offset, entry.alloc.size);
                    uint32 size = buffer.size();
                    gw2dt::compression::inflateDatFileBuffer(compressed.size(), compressed.data(), size, buffer.data());
                    assert(size == buffer.size());
                }
                else
                {
                    byte* p = buffer.data();
                    uint32 const blocks = (entry.alloc.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
                    for (uint32 i = 0; i < blocks; ++i)
                    {
                        Read(*p, entry.alloc.offset + i * BLOCK_SIZE, std::min<size_t>(BLOCK_DATA_SIZE, entry.alloc.size - i * BLOCK_SIZE - sizeof(uint32)));
                        p += BLOCK_DATA_SIZE;
                    }
                }
            }
        }
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
    static constexpr uint32 BLOCK_DATA_SIZE = BLOCK_SIZE - sizeof(uint32);

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

    struct CompressedFileHeader
    {
        uint32 Unknown0;
        uint32 UncompressedSize;
        uint32 Unknown8;
    };
};
#pragma pack(pop)

enum class Kind
{
    Game,
    Local,
};
struct Source;
struct File
{
    uint32 ID;
    std::reference_wrapper<Source> Source;

    std::strong_ordering operator<=>(File const& other) const;
    bool operator==(File const& other) const { return ID == other.ID && &Source.get() == &other.Source.get(); }
};
struct Source
{
    uint32 LoadOrder;
    Kind Kind;
    std::filesystem::path Path;
    Archive Archive;
    std::vector<File> Files;

    auto operator<=>(Source const& other) const { return LoadOrder <=> other.LoadOrder; }
};

}
