export module GW2Viewer.Data.Pack.PackFile:PackFile;
export import :Layout;
import GW2Viewer.Common;
import GW2Viewer.Common.FourCC;
import std;
import <cstddef>;
import <cstring>;

export namespace GW2Viewer::Data::Pack
{
namespace Layout::Traversal
{
struct QueryChunk;
}

#pragma pack(push, 1)
struct PackFileChunk
{
    struct ChunkHeader
    {
        fcc Magic;
        uint32 NextChunkOffset;
        uint16 Version;
        uint16 HeaderSize;
        uint32 DescriptorOffset;
    };

    ChunkHeader Header;
    byte Data[];

    PackFileChunk() = delete;
    PackFileChunk(PackFileChunk const&) = delete;
    PackFileChunk(PackFileChunk&&) = delete;
};
struct PackFile
{
    struct FileHeader
    {
        char Magic[2]; // signature PACKFILE_SIGNATURE
        uint16 FlagUnk1 : 1;
        uint16 FlagUnk2 : 1;
        uint16 Is64Bit : 1;
        uint16 Zero;
        uint16 HeaderSize;
        fcc ContentType;
    };

    static PackFile* Alloc(size_t size)
    {
        auto* file = (PackFile*)operator new(size + sizeof(PackFileChunk::ChunkHeader));
        memset(file, 0, size + sizeof(PackFileChunk::ChunkHeader));
        return file;
    }

    FileHeader Header; // hdr
    byte Data[];

    PackFile() = delete;
    PackFile(PackFile const&) = delete;
    PackFile(PackFile&&) = delete;

    template<typename T>
    class ChunkIteratorBase
    {
        T* m_pos;

    public:
        ChunkIteratorBase(T* pos) : m_pos(pos) { }
        ChunkIteratorBase(ChunkIteratorBase const&) = default;
        ChunkIteratorBase(ChunkIteratorBase&&) = default;
        ChunkIteratorBase& operator=(ChunkIteratorBase const&) = default;
        ChunkIteratorBase& operator=(ChunkIteratorBase&&) = default;

        bool operator==(ChunkIteratorBase const&) const = default;
        ChunkIteratorBase& operator++() { m_pos = (T*)((byte const*)m_pos + m_pos->Header.NextChunkOffset + offsetof(PackFileChunk::ChunkHeader, NextChunkOffset) + sizeof(m_pos->Header.NextChunkOffset)); return *this; }
        ChunkIteratorBase operator++(int) { auto copy = *this; ++*this; return copy; }

        T& operator*() const { return *m_pos; }
        T* operator->() const { return m_pos; }
    };
    using ChunkIterator = ChunkIteratorBase<PackFileChunk>;
    using ConstChunkIterator = ChunkIteratorBase<PackFileChunk const>;

    [[nodiscard]] PackFileChunk const& GetFirstChunk() const { return *begin(); }
    [[nodiscard]] PackFileChunk const& GetChunk(fcc magic) const
    {
        for (auto itr = begin(); ; ++itr)
            if (itr->Header.Magic == magic)
                return *itr;
    }
    [[nodiscard]] Layout::Traversal::QueryChunk QueryChunk(fcc magic) const;

    [[nodiscard]] ChunkIterator begin() { return (PackFileChunk*)&Data; }
    [[nodiscard]] ConstChunkIterator begin() const { return (PackFileChunk const*)&Data; }
    [[nodiscard]] ChunkIterator end()
    {
        auto itr = begin();
        while (itr->Header.Magic != fcc::Empty)
            ++itr;
        return itr;
    }
    [[nodiscard]] ConstChunkIterator end() const
    {
        auto itr = begin();
        while (itr->Header.Magic != fcc::Empty)
            ++itr;
        return itr;
    }
};
#pragma pack(pop)

}
