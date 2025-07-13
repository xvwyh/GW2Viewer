export module GW2Viewer.Data.Archive.Manager;
import GW2Viewer.Common;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.Utils.Async.ProgressBarContext;
import std;
import <boost/container/static_vector.hpp>;

export namespace GW2Viewer::Data::Archive
{

class Manager
{
public:
    [[nodiscard]] Source* GetSource(Kind kind = Kind::Game)
    {
        auto const itr = std::ranges::find(m_sources, kind, &Source::Kind);
        return itr != m_sources.end() ? itr.get_ptr() : nullptr;
    }
    [[nodiscard]] Archive* GetArchive(Kind kind = Kind::Game)
    {
        if (auto const source = GetSource(kind))
            return &source->Archive;
        return nullptr;
    }
    [[nodiscard]] auto const& GetFiles() const { return m_files; }
    [[nodiscard]] File const* GetFileEntry(uint32 fileID, Kind kind = Kind::Game) const
    {
        if (auto const itr = std::ranges::find(m_sources, kind, &Source::Kind); itr != m_sources.end())
            return itr->GetFile(fileID);

        return nullptr;
    }
    [[nodiscard]] std::vector<byte> GetFile(uint32 fileID)
    {
        std::vector<byte> buffer;
        for (auto& source : m_sources)
        {
            if (auto const size = source.Archive.GetFileSize(fileID))
            {
                buffer.resize(size);
                source.Archive.GetFile(fileID, buffer);
                if (buffer.size() == size)
                    break;
            }
        }
        return buffer;
    }
    [[nodiscard]] std::unique_ptr<Pack::PackFile> GetPackFile(uint32 fileID)
    {
        for (auto& source : m_sources)
            if (auto file = source.Archive.GetPackFile(fileID))
                return file;

        return nullptr;
    }

    [[nodiscard]] bool ContainsFile(uint32 fileID)
    {
        if (fileID > m_files.rbegin()->ID)
            return false;

        return std::ranges::binary_search(m_files, fileID, { }, &File::ID);
    }

    void Add(Kind kind, std::filesystem::path const& path);

    void Load(Utils::Async::ProgressBarContext& progress)
    {
        if (m_loaded)
            return;
        m_loaded = true;

        for (auto& source : m_sources)
        {
            source.Archive.Open(source.Path, progress);
            source.Files.assign_range(source.Archive.FileIdToMftEntry | std::views::transform([&](auto const& pair) -> File { return { pair.first, source, *pair.second }; }));
            for (auto const& file : source.Files)
                source.FileLookup.emplace(file.ID, file);
            m_files.insert_range(source.Files);
        }
    }

private:
    boost::container::static_vector<Source, 5> m_sources;
    std::set<File> m_files;
    bool m_loaded = false;
};

}
