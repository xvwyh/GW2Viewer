module;
#include <assert.h>

module GW2Viewer.Data.Manifest.Manager;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Data.Game;
import GW2Viewer.Utils.Encoding;

namespace GW2Viewer::Data::Manifest
{

void Manager::LoadRootManifest(uint32 fileID, Utils::Async::ProgressBarContext& progress)
{
    if (auto rootPackFile = G::Game.Archive.GetPackFile(fileID))
    {
        if (auto const& root = rootPackFile->QueryChunk(fcc::ARMF))
        {
            //assert((uint32)root["buildId"] == G::Game.Build);
            progress.Start(root["manifests[]"].GetArraySize());
            for (auto const& manifest : root["manifests"])
            {
                auto const manifestName = ((std::wstring_view)manifest["name"]).data();
                progress.SetDescription(std::format("Loading manifests:\n{}", Utils::Encoding::ToUTF8(manifestName)));
                LinkAssetVersions(manifest["baseId"], manifest["fileId"], manifest["size"], manifest["flags"], manifestName);
                LoadAssetManifest(manifest["baseId"], manifestName);
                ++progress;
            }
            for (auto const& extraFile : root["extraFiles"])
                LoadRootManifest(extraFile["baseId"], progress);
        }

        m_rootManifestPackFiles.emplace_back(std::move(rootPackFile));
    }
}

void Manager::LoadAssetManifest(uint32 fileID, wchar_t const* manifestName)
{
    if (auto const manifestPackFile = G::Game.Archive.GetPackFile(fileID))
    {
        if (auto const& manifest = manifestPackFile->QueryChunk(fcc::MFST))
        {
            //assert((uint32)manifest["buildId"] == G::Game.Build);
            for (auto const& record : manifest["records"])
                LinkAssetVersions(record["baseId"], record["fileId"], record["size"], record["flags"], manifestName);
            for (auto const& stream : manifest["streams"])
                LinkAssetStreams(stream["parentBaseId"], stream["streamBaseId"]);
            /*
            for (auto const properties = manifest["properties"]; auto const& propertyIndex : manifest["propertyTable"])
            {
                auto const& property = properties[propertyIndex["properyIndex"]];
                uint32 const type = property["type"];
                uint32 const data = *(uint32 const*)property["data[0]"].GetPointer();
                assert(!type);
                if (auto const fileEntry = G::Game.Archive.GetFileEntry(propertyIndex["baseId"]))
                    assert(data == fileEntry->GetMetadata().StreamBaseID);
            }
            */
        }
    }
}

void Manager::LinkAssetVersions(uint32 baseId, uint32 fileId, uint32 size, uint32 flags, wchar_t const* manifestName)
{
    if (auto const file = G::Game.Archive.GetFileEntry(baseId))
    {
        auto& asset = file->GetManifestAsset();
        //assert(&asset == &G::Game.Archive.GetFileEntry(fileId)->GetManifestAsset());
        asset.BaseID = baseId;
        asset.FileID = fileId;
        asset.Size = size;
        asset.Flags = flags;
        asset.ManifestNames.emplace(manifestName);
    }
}

void Manager::LinkAssetStreams(uint32 parentBaseId, uint32 streamBaseId)
{
    if (auto const file = G::Game.Archive.GetFileEntry(parentBaseId))
        file->GetManifestAsset().StreamBaseID = streamBaseId;
    if (auto const file = G::Game.Archive.GetFileEntry(streamBaseId))
        file->GetManifestAsset().ParentBaseID = parentBaseId;
}

}
