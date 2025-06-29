export module GW2Viewer.Data.Pack.Manager;
import GW2Viewer.Common;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.Utils.Container;
import GW2Viewer.Utils.ProgressBarContext;
import std;

export namespace Data::Pack
{

class Manager
{
public:
    auto GetChunk(std::string_view name) const { return Utils::Container::Find(m_chunks, name); }

    void Load(std::filesystem::path const& path, ProgressBarContext& progress);
    bool IsLoaded() const { return m_loaded; }

private:
    bool m_loaded = false;
    std::unordered_map<byte const*, Layout::Type> m_types;
    std::map<std::string, std::map<uint32, Layout::Type const*>, std::less<>> m_chunks;
};

}
