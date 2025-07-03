module;
#include <cwctype>

module GW2Viewer.Data.Content;
import :ContentFilter;
import :ContentObject;
import GW2Viewer.User.Config;
import std;

namespace GW2Viewer::Data::Content
{

bool ContentNamespace::HasCustomName() const
{
    return G::Config.ContentNamespaceNames.contains(GetFullName());
}

std::wstring ContentNamespace::GetDisplayName(bool skipCustom, bool skipColor) const
{
    if (skipCustom)
        return skipColor ? Name : std::format(L"<c=#FFC>{}</c>", Name);

    auto const itr = G::Config.ContentNamespaceNames.find(GetFullName());
    return itr != G::Config.ContentNamespaceNames.end() && !itr->second.empty() ? itr->second : GetDisplayName(true, skipColor);
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

}
