export module GW2Viewer.Data.Manifest.Asset;
import GW2Viewer.Common;
import <boost/container/flat_set.hpp>;
import <boost/container/small_vector.hpp>;

export namespace GW2Viewer::Data::Manifest
{

struct Asset
{
    boost::container::small_flat_set<wchar_t const*, 20> ManifestNames;
    uint32 BaseID = 0; // ID of the file when it was first added to the archive
    uint32 FileID = 0; // ID of the latest version of the file, only 1 latest version is retained in the archive
    uint32 Size = 0;
    uint32 Flags = 0;
    uint32 ParentBaseID = 0; // ID of the lower quality version of the file
    uint32 StreamBaseID = 0; // ID of the higher quality version of the file
};

}
