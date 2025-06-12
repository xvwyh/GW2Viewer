#include "Content.h"

#include "Config.h"
#include "Encryption.h"
#include "PackContent.h"
#include "StringsFile.h"
#include "Symbols.h"

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <picosha2.h>

#include <algorithm>
#include <cassert>
#include <cwctype>

STATIC(g_contentRoot) = nullptr;
STATIC(g_contentTypeInfos);
STATIC(g_contentObjects);
STATIC(g_rootedContentObjects);
STATIC(g_unrootedContentObjects);
STATIC(g_contentObjectsByDataPointer);
STATIC(g_contentObjectsByGUID);
STATIC(g_contentObjectsByName);
STATIC(g_contentNamespacesByName);
STATIC(g_contentNamespaces);
STATIC(g_contentLoaded) = false;

std::wstring ContentTypeInfo::GetDisplayName() const
{
    auto const itr = g_config.TypeInfo.find(Index);
    return itr != g_config.TypeInfo.end() && !itr->second.Name.empty()
        ? to_wstring(itr->second.Name)
        : std::format(L"#{}", Index);
}

bool ContentNamespace::HasCustomName() const
{
    return g_config.ContentNamespaceNames.contains(GetFullName());
}

std::wstring ContentNamespace::GetDisplayName(bool skipCustom, bool skipColor) const
{
    if (skipCustom)
        return skipColor ? Name : std::format(L"<c=#FFC>{}</c>", Name);

    auto const itr = g_config.ContentNamespaceNames.find(GetFullName());
    return itr != g_config.ContentNamespaceNames.end() && !itr->second.empty() ? itr->second : GetDisplayName(true, skipColor);
}

std::wstring ContentNamespace::GetFullDisplayName(bool skipCustom, bool skipColor) const
{
    return Parent
        ? std::format(L"{}.{}", Parent->GetFullDisplayName(skipCustom, skipColor), GetDisplayName(skipCustom, skipColor))
        : GetDisplayName(skipCustom, skipColor);
}

std::wstring ContentNamespace::GetFullName() const
{
    return Parent
        ? std::format(L"{}.{}", Parent->Name, Name)
        : Name;
}

ContentNamespace const* ContentNamespace::GetRoot() const
{
    auto* current = this;
    while (auto const* parent = current->Parent)
        current = parent;
    return current;
}

bool ContentNamespace::MatchesFilter(ContentFilter& filter) const
{
    auto& result = filter.FilteredNamespaces[Index];
    if (result == ContentFilter::UNCACHED_RESULT)
    {
        std::wstring displayName;
        result =
            !filter ||
            !filter.NameSearch.empty() && std::ranges::search(Name, filter.NameSearch, std::ranges::equal_to(), std::towupper, std::towupper) ||
            !filter.NameSearch.empty() && (displayName = GetDisplayName(), std::ranges::search(displayName, filter.NameSearch, std::ranges::equal_to(), std::towupper, std::towupper)) ||
            std::ranges::any_of(Namespaces, std::bind_back(&ContentNamespace::MatchesFilter, std::ref(filter))) ||
            std::ranges::any_of(Entries, std::bind_back(&ContentObject::MatchesFilter, std::ref(filter)));
    }
    return result;
}

void ContentObject::AddReference(ContentObject& target, Reference::Types type)
{
    if (Reference reference { &target, type }; !std::ranges::contains(OutgoingReferences, reference))
        OutgoingReferences.emplace_back(reference);
    if (Reference reference { this, type }; !std::ranges::contains(target.IncomingReferences, reference))
        target.IncomingReferences.emplace_back(reference);
}

void ContentObject::Finalize()
{
    if (Data.size() != UNINITIALIZED_SIZE)
        return;

    if (Data.size() == UNINITIALIZED_SIZE)
    {
        if (auto* name = GetName(); name)
        {
            if (auto* ptr = (byte const*)name->Name)
                if (ptr > Data.data() /*&& ptr < Data.data() + Data.size()*/)
                    Data = { Data.data(), ptr };

            if (auto* ptr = (byte const*)name->FullName)
                if (ptr > Data.data() /*&& ptr < Data.data() + Data.size()*/)
                    Data = { Data.data(), ptr };
        }
    }
    if (Data.size() == UNINITIALIZED_SIZE)
    {
        auto const itr = std::ranges::upper_bound(*ContentFileEntryBoundaries, ContentFileEntryOffset);
        assert(itr != ContentFileEntryBoundaries->end());
        Data = { Data.data(), Data.data() + (*itr - ContentFileEntryOffset) };
    }
    if (Data.size() == UNINITIALIZED_SIZE)
        Data = { Data.data(), 1 };
}

bool ContentObject::HasCustomName() const
{
    return g_config.ContentObjectNames.contains(*GetGUID());
}

std::wstring ContentObject::GetDebugDisplayName() const
{
    return std::format(L"[{}] {}", Type->GetDisplayName(), GetDisplayName());
}

std::wstring ContentObject::GetDisplayName(bool skipCustom, bool skipColor) const
{
    if (!skipCustom)
    {
        // Use custom name if set
        if (auto const itr = g_config.ContentObjectNames.find(*GetGUID()); itr != g_config.ContentObjectNames.end() && !itr->second.empty())
        {
            if (!skipColor)
                if (auto* name = GetName(); name && name->Name && *name->Name)
                    return std::format(L"<c=#{}>{}</c>", Content::MangleFullName(itr->second) == wcsrchr(*name->Name, L'.') + 1 ? L"CFC" : L"FCC", itr->second);
            return itr->second;
        }

        // Use name from a designated symbol is enabled and available
        if (auto const itr = g_config.TypeInfo.find(Type->Index); itr != g_config.TypeInfo.end())
        {
            bool wasEncrypted = false;
            auto const encryptedText = GetEncryptionStatusText(EncryptionStatus::Encrypted);
            for (auto const& typeInfo = itr->second; auto const& field : typeInfo.NameFields)
            {
                for (auto& result : QuerySymbolData(*(ContentObject*)this, field)) // TODO: Fix constness
                {
                    std::string value;
                    auto const symbolType = result.Symbol->GetType();
                    if (auto text = symbolType->GetDisplayText(result.Data); !text.empty())
                        value = std::move(text);
                    else if (auto const content = symbolType->GetContent(result.Data).value_or(nullptr))
                        value = to_utf8(content->GetDisplayName(false, true));

                    if (value == encryptedText)
                    {
                        wasEncrypted = true;
                        continue;
                    }

                    if (!value.empty())
                        return from_utf8(wasEncrypted ? encryptedText + value : value);
                }
            }
        }
    }
    if (auto* name = GetName(); name && name->Name && *name->Name)
        return std::vformat(skipColor ? L"{}" : L"<c=#FFC>{}</c>", std::make_wformat_args(*name->Name));
    if (auto* id = GetDataID())
        return std::vformat(skipColor ? L"<ID: 0x{:08X}>" : L"<c=#AAA><ID: 0x{:08X}></c>", std::make_wformat_args(Type->Index << 22 | (*id & 0x3FFFFF)));
    if (auto* uid = GetUID(); uid && *uid)
        return std::vformat(skipColor ? L"<UID: 0x{:08X}>" : L"<c=#AAA><UID: 0x{:08X}></c>", std::make_wformat_args(Type->Index << 22 | (*uid & 0x3FFFFF)));
    if (auto* guid = GetGUID())
        return std::vformat(skipColor ? L"<GUID: {}>" : L"<c=#AAA><GUID: {}></c>", std::make_wformat_args(*guid));
    return std::vformat(skipColor ? L"<@0x{:016X}>" : L"<c=#AAA><@0x{:016X}></c>", std::make_wformat_args((uintptr_t)Data.data()));
}

std::wstring ContentObject::GetFullDisplayName(bool skipCustom, bool skipColor) const
{
    if (auto* name = GetName(); name && name->FullName && *name->FullName && (!name->Name || !*name->Name || std::wstring_view(*name->Name) != *name->FullName))
        return *name->FullName;
    return Namespace
        ? std::vformat(skipColor ? L"{}.{}" : L"<c=#8>{}.</c>{}", std::make_wformat_args(Namespace->GetFullDisplayName(), GetDisplayName(skipCustom, skipColor)))
        : GetDisplayName(skipCustom, skipColor);
}

std::wstring ContentObject::GetFullName() const
{
    if (auto* name = GetName())
    {
        if (name->FullName && *name->FullName)
            return *name->FullName;

        if (name->Name && *name->Name)
            return Namespace
                ? std::format(L"{}.{}", Namespace->Name, *name->Name)
                : *name->Name;
    }
    return { };
}

uint32 ContentObject::GetIcon() const
{
    if (auto const itr = g_config.TypeInfo.find(Type->Index); itr != g_config.TypeInfo.end())
    {
        for (auto const& typeInfo = itr->second; auto const& field : typeInfo.IconFields)
        {
            for (auto& result : QuerySymbolData(*(ContentObject*)this, field)) // TODO: Fix constness
            {
                uint32 value = 0;
                auto const symbolType = result.Symbol->GetType();
                if (auto const icon = symbolType->GetIcon(result.Data).value_or(0))
                    value = icon;
                else if (auto const content = symbolType->GetContent(result.Data).value_or(nullptr))
                    value = content->GetIcon();

                if (value)
                    return value;
            }
        }
    }

    return { };
}

ContentObject* ContentObject::GetMap() const
{
    if (auto const itr = g_config.TypeInfo.find(Type->Index); itr != g_config.TypeInfo.end())
    {
        for (auto const& typeInfo = itr->second; auto const& field : typeInfo.MapFields)
        {
            for (auto& result : QuerySymbolData(*(ContentObject*)this, field)) // TODO: Fix constness
            {
                ContentObject* value = nullptr;
                auto const symbolType = result.Symbol->GetType();
                if (auto const map = symbolType->GetMap(result.Data).value_or(nullptr))
                    value = map;
                else if (auto const content = symbolType->GetContent(result.Data).value_or(nullptr))
                    value = content->GetMap();

                if (value)
                    return value;
            }
        }
    }

    return (ContentObject*)this; // TODO: Fix constness
}

bool ContentObject::MatchesFilter(ContentFilter& filter) const
{
    auto& result = filter.FilteredObjects[Index];
    if (result == ContentFilter::UNCACHED_RESULT)
    {
        ContentName const* name;
        GUID const* guid;
        uint32 const* id;
        std::wstring displayName;
        result =
            !filter ||
            std::ranges::any_of(Entries, std::bind_back(&ContentObject::MatchesFilter, std::ref(filter))) ||
            (filter.TypeIndex < 0 || Type->Index == filter.TypeIndex) &&
            (filter.NameSearch.empty()
                || (name = GetName(), name && name->Name && *name->Name && std::ranges::search(std::wstring_view(*name->Name), filter.NameSearch, std::ranges::equal_to(), std::towupper, std::towupper))
                || (displayName = GetDisplayName(), std::ranges::search(displayName, filter.NameSearch, std::ranges::equal_to(), std::towupper, std::towupper))) &&
            (!filter.GUIDSearch || (guid = GetGUID(), guid && *guid == *filter.GUIDSearch)) &&
            (!filter.UIDSearch || (id = GetUID(), id && *id >= filter.UIDSearch->first && *id <= filter.UIDSearch->second)) &&
            (!filter.DataIDSearch || (id = GetDataID(), id && *id >= filter.DataIDSearch->first && *id <= filter.DataIDSearch->second));
    }
    return result;
}

ContentTypeInfo* GetContentTypeInfo(Content::EContentTypes type)
{
    if (auto const itr = std::ranges::find(g_config.TypeInfo, type, [](auto const& pair) { return pair.second.ContentType; }); itr != g_config.TypeInfo.end())
        if (itr->first < g_contentTypeInfos.size())
            return g_contentTypeInfos.at(itr->first).get();

    return nullptr;
}

ContentObject* GetContentObjectByDataID(Content::EContentTypes type, uint32 dataID)
{
    if (auto const typeInfo = GetContentTypeInfo(type))
    {
        if (typeInfo->DataIDOffset < 0)
            std::terminate();

        for (auto const object : typeInfo->Objects)
            if (*object->GetDataID() == dataID)
                return object;
    }

    return nullptr;
}

uint64 Content::MangleToNumber(std::wstring_view name, picosha2::hash256_one_by_one& hasher)
{
    std::array<byte, 32> hash;
    //picosha2::hash256(std::string_view { (char const*)name.data(), 2 * name.length() }, hash);
    hasher.init();
    hasher.process((char const*)name.data(), (char const*)name.data() + 2 * name.length());
    hasher.finish();
    hasher.get_hash_bytes(hash.begin(), hash.end());

    auto pHash = (uint32 const*)hash.data();
    uint64 fnv = 0xCBF29CE484222325ULL;
    for (int i = 0; i < 8; ++i)
    {
        uint32 chunk = *pHash++;
        for (int j = 0; j < 4; ++j)
        {
            fnv = (fnv ^ chunk) * 0x100000001B3ULL;
            chunk >>= 8;
        }
    }
    return fnv;
}

uint32 Content::DemangleToNumber(std::wstring_view mangledName)
{
    uint32 fnv = 0;
    using namespace boost::archive::iterators;
    using iterator = transform_width<binary_from_base64<decltype(mangledName)::const_iterator>, 8, 6>;
    std::copy(iterator(mangledName.begin()), iterator(mangledName.end()), (byte*)&fnv);
    return fnv;
}

void Content::Mangle(std::wstring_view name, wchar_t* dest, size_t chars, picosha2::hash256_one_by_one& hasher)
{
    uint64 const fnv = MangleToNumber(name, hasher);
    using namespace boost::archive::iterators;
    using iterator = base64_from_binary<transform_width<byte const*, 6, 8>>;
    std::copy_n(iterator((byte const*)&fnv), chars - 1, dest);
    dest[chars - 1] = L'\0';
}

void Content::MangleFullName(std::wstring_view name, wchar_t* dest, uint32 chars, picosha2::hash256_one_by_one& hasher)
{
    assert(chars >= Content::MANGLE_FULL_NAME_BUFFER_SIZE);
    if (auto const pos = name.find_last_of(L'.'); pos != std::wstring_view::npos)
    {
        Mangle(name.substr(0, pos), dest, 6, hasher);
        dest[5] = '.';
        Mangle(name.substr(pos + 1), dest + 6, 6, hasher);
    }
    else
        Mangle(name, dest, 6, hasher);
}

std::wstring Content::MangleFullName(std::wstring_view name)
{
    picosha2::hash256_one_by_one hasher;
    std::wstring mangled(Content::MANGLE_FULL_NAME_BUFFER_SIZE, L'\0');
    MangleFullName(name, mangled.data(), mangled.length() + 1, hasher);
    mangled.resize(wcslen(mangled.c_str()));
    return mangled;
}
