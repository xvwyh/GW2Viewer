module;
#include <cassert>
#include <cwctype>

module GW2Viewer.Data.Content;
import GW2Viewer.Data.Content.Mangling;
import GW2Viewer.Data.Encryption;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Format;
import std;

namespace GW2Viewer::Data::Content
{

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
    return G::Config.ContentObjectNames.contains(*GetGUID());
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
        if (auto const itr = G::Config.ContentObjectNames.find(*GetGUID()); itr != G::Config.ContentObjectNames.end() && !itr->second.empty())
        {
            if (!skipColor)
                if (auto* name = GetName(); name && name->Name && *name->Name)
                    return std::format(L"<c=#{}>{}</c>", MangleFullName(itr->second) == wcsrchr(*name->Name, L'.') + 1 ? L"CFC" : L"FCC", itr->second);
            return itr->second;
        }

        // Use name from a designated symbol if enabled and available
        if (auto const itr = G::Config.TypeInfo.find(Type->Index); itr != G::Config.TypeInfo.end())
        {
            bool wasEncrypted = false;
            auto const encryptedText = GetStatusText(Encryption::Status::Encrypted);
            for (auto const& typeInfo = itr->second; auto const& field : typeInfo.NameFields)
            {
                for (auto& result : QuerySymbolData(*(ContentObject*)this, field)) // TODO: Fix constness
                {
                    std::string value;
                    auto const symbolType = result.Symbol->GetType();
                    if (auto text = symbolType->GetDisplayText(result.Data); !text.empty())
                        value = std::move(text);
                    else if (auto const content = symbolType->GetContent(result.Data).value_or(nullptr))
                        value = Utils::Encoding::ToUTF8(content->GetDisplayName(false, true));

                    if (value == encryptedText)
                    {
                        wasEncrypted = true;
                        continue;
                    }

                    if (!value.empty())
                        return Utils::Encoding::FromUTF8(wasEncrypted ? encryptedText + value : value);
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
        ? std::vformat(skipColor ? L"{}.{}" : L"<c=#8>{}.</c>{}", std::make_wformat_args(Namespace->GetFullDisplayName(skipCustom, skipColor), GetDisplayName(skipCustom, skipColor)))
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
    if (auto const itr = G::Config.TypeInfo.find(Type->Index); itr != G::Config.TypeInfo.end())
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
    if (auto const itr = G::Config.TypeInfo.find(Type->Index); itr != G::Config.TypeInfo.end())
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
    if (!filter.IsFilteringObjects())
        return true;

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
            (!filter.Type || Type == filter.Type) &&
            (filter.NameSearch.empty()
                || (name = GetName(), name && name->Name && *name->Name && std::ranges::search(std::wstring_view(*name->Name), filter.NameSearch, std::ranges::equal_to(), std::towupper, std::towupper))
                || (displayName = GetDisplayName(), std::ranges::search(displayName, filter.NameSearch, std::ranges::equal_to(), std::towupper, std::towupper))) &&
            (!filter.GUIDSearch || (guid = GetGUID(), guid && *guid == *filter.GUIDSearch)) &&
            (!filter.UIDSearch || (id = GetUID(), id && *id >= filter.UIDSearch->first && *id <= filter.UIDSearch->second)) &&
            (!filter.DataIDSearch || (id = GetDataID(), id && *id >= filter.DataIDSearch->first && *id <= filter.DataIDSearch->second));
    }
    return result;
}

}
