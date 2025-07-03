export module GW2Viewer.Data.Content:ContentFilter;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import std;

export namespace GW2Viewer::Data::Content
{

struct ContentFilter
{
    static constexpr size_t UNCACHED_RESULT = std::numeric_limits<size_t>::max();

    int32 TypeIndex = -1;
    std::wstring NameSearch;
    std::optional<GUID> GUIDSearch;
    std::optional<std::pair<uint32, uint32>> UIDSearch;
    std::optional<std::pair<uint32, uint32>> DataIDSearch;

    std::vector<size_t> FilteredNamespaces;
    std::vector<size_t> FilteredObjects;

    operator bool() const { return TypeIndex != -1 || !NameSearch.empty() || GUIDSearch || UIDSearch || DataIDSearch; }
};

}
