export module GW2Viewer.Data.Content:ContentNamespace;
import GW2Viewer.Common;
import std;

export namespace GW2Viewer::Data::Content
{
struct ContentFilter;
struct ContentObject;

struct ContentNamespace
{
    uint32 Index;
    int32 Domain;
    std::wstring Name;
    ContentNamespace const* Parent { };
    std::list<std::unique_ptr<ContentNamespace>> Namespaces;
    std::list<std::unique_ptr<ContentObject>> Entries;

    [[nodiscard]] bool HasCustomName() const;
    [[nodiscard]] std::wstring GetDisplayName(bool skipCustom = false, bool skipColor = false) const;
    [[nodiscard]] std::wstring GetFullDisplayName(bool skipCustom = false, bool skipColor = false) const;
    [[nodiscard]] std::wstring GetFullName() const;

    [[nodiscard]] ContentNamespace const* GetRoot() const;

    bool MatchesFilter(ContentFilter& filter) const;
};

}
