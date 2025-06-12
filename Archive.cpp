#include "Archive.h"

#include "PackFile.h"
#include "ProgressBarContext.h"

#include <gw2dattools/compression/inflateDatFileBuffer.h>

#include <cassert>
#include <filesystem>

bool Archive::Open(std::filesystem::path const& path, ProgressBarContext& progress)
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

constexpr uint32 BLOCK_SIZE = 0x10000;
constexpr uint32 BLOCK_DATA_SIZE = BLOCK_SIZE - sizeof(uint32);

uint32 Archive::GetFileSize(uint32 fileID)
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

void Archive::GetFile(uint32 fileID, std::span<byte> buffer)
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

std::unique_ptr<pf::PackFile> Archive::GetPackFile(uint32 fileID)
{
    std::unique_ptr<pf::PackFile> result;
    if (auto size = GetFileSize(fileID))
    {
        result.reset(pf::PackFile::Alloc(size));
        GetFile(fileID, { (byte*)result.get(), size });
    }
    return result;
}
