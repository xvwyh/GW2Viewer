export module GW2Viewer.Data.Content:ContentFilter;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import std;

export namespace GW2Viewer::Data::Content
{
struct ContentTypeInfo;

struct ContentFilter
{
    static constexpr size_t UNCACHED_RESULT = std::numeric_limits<size_t>::max();

    ContentTypeInfo const* Type { };
    std::wstring NameSearch;
    std::optional<GUID> GUIDSearch;
    std::optional<std::pair<uint32, uint32>> UIDSearch;
    std::optional<std::pair<uint32, uint32>> DataIDSearch;

    std::vector<size_t> FilteredNamespaces;
    std::vector<size_t> FilteredObjects;

    [[nodiscard]] auto IsFilteringNamespaces() const { return !FilteredNamespaces.empty(); }
    [[nodiscard]] auto IsFilteringObjects() const { return !FilteredObjects.empty(); }

    operator bool() const { return Type || !NameSearch.empty() || GUIDSearch || UIDSearch || DataIDSearch; }
};

}
