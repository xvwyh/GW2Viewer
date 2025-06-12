#pragma once
#include "Common.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <vector>

class ProgressBarContext;

#pragma pack(push, 1)
namespace pf
{
struct PackFile;
}

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

    bool Open(std::filesystem::path const& path, ProgressBarContext& progress);

    [[nodiscard]] auto GetFileIDs() const { return FileIdToMftEntry | std::ranges::views::keys; }
    uint32 GetFileSize(uint32 fileID);
    std::vector<byte> GetFile(uint32 fileID)
    {
        std::vector<byte> result(GetFileSize(fileID));
        GetFile(fileID, result);
        return result;
    }
    void GetFile(uint32 fileID, std::span<byte> buffer);
    std::unique_ptr<pf::PackFile> GetPackFile(uint32 fileID);

private:
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
