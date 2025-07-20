export module GW2Viewer.Data.Manifest.Manager;
import GW2Viewer.Common;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.Utils.Async.ProgressBarContext;
import std;

export namespace GW2Viewer::Data::Manifest
{

struct Manager
{
    void Load(Utils::Async::ProgressBarContext& progress)
    {
        progress.Start("Loading manifests");
        LoadRootManifest(4101, progress);
    }

private:
    std::vector<std::unique_ptr<Pack::PackFile>> m_rootManifestPackFiles;

    void LoadRootManifest(uint32 fileID, Utils::Async::ProgressBarContext& progress);
    void LoadAssetManifest(uint32 fileID, wchar_t const* manifestName);

    void LinkAssetVersions(uint32 baseId, uint32 fileId, uint32 size, uint32 flags, wchar_t const* manifestName);
    void LinkAssetStreams(uint32 parentBaseId, uint32 streamBaseId);
};

}
