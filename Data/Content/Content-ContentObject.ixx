export module GW2Viewer.Data.Content:ContentObject;
import :ContentFilter;
import :ContentName;
import :ContentNamespace;
import :ContentTypeInfo;
import :Query;
import GW2Viewer.Common;
import std;

export namespace GW2Viewer::Data::Content
{
struct ContentTypeInfo;
struct ContentNamespace;

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

    template<typename T>
    [[nodiscard]] T const* GetAtOffset(int32 offset) const
    {
        return offset >= 0 ? (T const*)&Data[offset] : nullptr;
    }

    [[nodiscard]] bool HasCustomName() const;
    [[nodiscard]] std::wstring GetDebugDisplayName() const;
    [[nodiscard]] std::wstring GetDisplayName(bool skipCustom = false, bool skipColor = false) const;
    [[nodiscard]] std::wstring GetFullDisplayName(bool skipCustom = false, bool skipColor = false) const;
    [[nodiscard]] std::wstring GetFullName() const;
    [[nodiscard]] uint32 GetIcon() const;
    [[nodiscard]] ContentObject* GetMap() const;
    [[nodiscard]] GUID const* GetGUID() const { return GetAtOffset<GUID>(Type->GUIDOffset); }
    [[nodiscard]] uint32 const* GetUID() const { return GetAtOffset<uint32>(Type->UIDOffset); }
    [[nodiscard]] uint32 const* GetDataID() const { return GetAtOffset<uint32>(Type->DataIDOffset); }
    [[nodiscard]] ContentName const* GetName() const { return GetAtOffset<ContentName>(Type->NameOffset); }

    QuerySymbolDataResult::Generator operator[](std::string_view path) { return QuerySymbolData(*this, path); }

    bool MatchesFilter(ContentFilter& filter) const;
};

}
