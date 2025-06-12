#pragma once
#include "Archive.h"

#include <boost/container/static_vector.hpp>

#include <set>

enum class ArchiveKind
{
    Game,
    Local,
};
struct ArchiveSource;
struct ArchiveFile
{
    uint32 ID;
    std::reference_wrapper<ArchiveSource> Source;

    std::strong_ordering operator<=>(ArchiveFile const& other) const;
    bool operator==(ArchiveFile const& other) const { return ID == other.ID && &Source.get() == &other.Source.get(); }
};
struct ArchiveSource
{
    uint32 LoadOrder;
    ArchiveKind Kind;
    std::filesystem::path Path;
    Archive Archive;
    std::vector<ArchiveFile> Files;

    auto operator<=>(ArchiveSource const& other) const { return LoadOrder <=> other.LoadOrder; }
};
class ArchiveManager
{
public:
    [[nodiscard]] Archive* GetArchive(ArchiveKind kind = ArchiveKind::Game)
    {
        auto const itr = std::ranges::find(m_sources, kind, &ArchiveSource::Kind);
        return itr != m_sources.end() ? &itr->Archive : nullptr;
    }
    [[nodiscard]] auto const& GetFiles() const { return m_files; }
    [[nodiscard]] ArchiveFile const* GetFileEntry(uint32 fileID, ArchiveKind kind = ArchiveKind::Game) const
    {
        if (auto const itr = std::ranges::find(m_sources, kind, &ArchiveSource::Kind); itr != m_sources.end())
            if (auto const range = std::ranges::equal_range(itr->Files, fileID, { }, &ArchiveFile::ID))
                return &range.front();

        return nullptr;
    }
    [[nodiscard]] std::vector<byte> GetFile(uint32 fileID);
    [[nodiscard]] std::unique_ptr<pf::PackFile> GetPackFile(uint32 fileID);

    [[nodiscard]] bool ContainsFile(uint32 fileID);

    void Add(ArchiveKind kind, std::filesystem::path path);

    void Load(ProgressBarContext& progress);

private:
    boost::container::static_vector<ArchiveSource, 5> m_sources;
    std::set<ArchiveFile> m_files;
    bool m_loaded = false;
};

inline std::strong_ordering ArchiveFile::operator<=>(ArchiveFile const& other) const
{
    if (auto const result = ID <=> other.ID; result != std::strong_ordering::equal) return result;
    if (auto const result = Source.get() <=> other.Source.get(); result != std::strong_ordering::equal) return result;
    return std::strong_ordering::equal;
}

extern ArchiveManager g_archives;
