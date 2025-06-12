#pragma once
#include "Common.h"
#include "ContentQuery.h"
#include "GUID.h"

#include <list>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace pf { struct PackFileChunk; }
namespace picosha2 { class hash256_one_by_one; }
namespace Content { enum EContentTypes : int; }
struct ContentObject;

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

struct ContentTypeInfo
{
    uint32 Index;
    int32 GUIDOffset;
    int32 UIDOffset;
    int32 DataIDOffset;
    int32 NameOffset;
    bool TrackReferences;
    std::list<ContentObject*> Objects;

    [[nodiscard]] std::wstring GetDisplayName() const;

    template<typename T>
    [[nodiscard]] static T const* GetAtContentOffset(ContentObject const& content, int32 offset);
};

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

struct ContentName
{
    wchar_t const* const* Name;
    wchar_t const* const* FullName;
};

struct ContentObject
{
    static constexpr size_t UNINITIALIZED_SIZE = 999999999;

    uint32 Index;
    ContentTypeInfo const* Type { };
    ContentNamespace const* Namespace { };
    ContentObject* Root { };
    std::list<std::unique_ptr<ContentObject>> Entries;
    std::span<byte const> Data;

    struct Reference
    {
        enum class Types
        {
            Root,
            Tracked,
            All,
        };
        ContentObject* Object;
        Types Type;

        bool operator==(Reference const&) const = default;
    };
    std::vector<Reference> OutgoingReferences;
    std::vector<Reference> IncomingReferences;
    void AddReference(ContentObject& target, Reference::Types type);

    uint32 const ContentFileEntryOffset;
    std::set<size_t> const* const ContentFileEntryBoundaries;
    byte const* ByteMap;

    void Finalize();

    [[nodiscard]] bool HasCustomName() const;
    [[nodiscard]] std::wstring GetDebugDisplayName() const;
    [[nodiscard]] std::wstring GetDisplayName(bool skipCustom = false, bool skipColor = false) const;
    [[nodiscard]] std::wstring GetFullDisplayName(bool skipCustom = false, bool skipColor = false) const;
    [[nodiscard]] std::wstring GetFullName() const;
    [[nodiscard]] uint32 GetIcon() const;
    [[nodiscard]] ContentObject* GetMap() const;
    [[nodiscard]] GUID const* GetGUID() const { return ContentTypeInfo::GetAtContentOffset<GUID>(*this, Type->GUIDOffset); }
    [[nodiscard]] uint32 const* GetUID() const { return ContentTypeInfo::GetAtContentOffset<uint32>(*this, Type->UIDOffset); }
    [[nodiscard]] uint32 const* GetDataID() const { return ContentTypeInfo::GetAtContentOffset<uint32>(*this, Type->DataIDOffset); }
    [[nodiscard]] ContentName const* GetName() const { return ContentTypeInfo::GetAtContentOffset<ContentName>(*this, Type->NameOffset); }

    QuerySymbolDataResult::Generator operator[](std::string_view path) { return QuerySymbolData(*this, path); }

    bool MatchesFilter(ContentFilter& filter) const;
};

template<typename T>
T const* ContentTypeInfo::GetAtContentOffset(ContentObject const& content, int32 offset)
{
    return offset >= 0 ? (T const*)&content.Data[offset] : nullptr;
}

extern ContentNamespace* g_contentRoot;
extern std::vector<std::unique_ptr<ContentTypeInfo>> g_contentTypeInfos;
extern std::vector<ContentObject*> g_contentObjects;
extern std::vector<ContentObject*> g_rootedContentObjects;
extern std::vector<ContentObject*> g_unrootedContentObjects;
extern std::unordered_map<byte const*, ContentObject*> g_contentObjectsByDataPointer;
extern std::unordered_map<GUID, ContentObject*> g_contentObjectsByGUID;
extern std::unordered_map<std::wstring_view, std::vector<ContentObject*>> g_contentObjectsByName;
extern std::unordered_map<std::wstring_view, std::vector<ContentNamespace*>> g_contentNamespacesByName;
extern std::vector<ContentNamespace*> g_contentNamespaces;
extern bool g_contentLoaded;

ContentTypeInfo* GetContentTypeInfo(Content::EContentTypes type);
inline ContentObject* GetContentObjectByGUID(GUID const& guid) { if (auto const object = FindInMap(g_contentObjectsByGUID, guid)) return *object; return nullptr; }
inline ContentObject* GetContentObjectByDataPointer(byte const* ptr) { if (auto const object = FindInMap(g_contentObjectsByDataPointer, ptr)) return *object; return nullptr; }
ContentObject* GetContentObjectByDataID(Content::EContentTypes type, uint32 dataID);

namespace Content
{
uint64 MangleToNumber(std::wstring_view name, picosha2::hash256_one_by_one& hasher);
uint32 DemangleToNumber(std::wstring_view mangledName);
void Mangle(std::wstring_view name, wchar_t* dest, size_t chars, picosha2::hash256_one_by_one& hasher);
void MangleFullName(std::wstring_view name, wchar_t* dest, uint32 chars, picosha2::hash256_one_by_one& hasher);
std::wstring MangleFullName(std::wstring_view name);
}
