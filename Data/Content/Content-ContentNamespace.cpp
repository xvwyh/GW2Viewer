module;
#include <cwctype>

module GW2Viewer.Data.Content;
import :ContentFilter;
import :ContentObject;
import GW2Viewer.Data.Content.Mangling;
import GW2Viewer.User.Config;
import std;

namespace GW2Viewer::Data::Content
{

std::wstring* GetCustomName(ContentNamespace const& ns)
{
    auto const itr = G::Config.ContentNamespaceNames.find(ns.GetFullName());
    return itr != G::Config.ContentNamespaceNames.end() && !itr->second.empty() ? &itr->second : nullptr;
}
bool IsCustomNameCorrect(ContentNamespace const& ns)
{
    return MangleFullName(std::format(L"{}.", ns.GetFullDisplayName(false, true))).substr(0, 5) == ns.Name;
}

bool ContentNamespace::HasCustomName() const
{
    return GetCustomName(*this);
}

bool ContentNamespace::HasCorrectCustomName() const
{
    return GetCustomName(*this) && IsCustomNameCorrect(*this);
}

std::wstring ContentNamespace::GetDisplayName(bool skipCustom, bool skipColor) const
{
    if (!skipCustom)
    {
        if (auto const custom = GetCustomName(*this))
        {
            if (!skipColor)
                if (!IsCustomNameCorrect(*this))
                    return std::format(L"<c=#FCC>{}</c>", *custom);
            return *custom;
        }
    }

    return skipColor ? Name : std::format(L"<c=#FFC>{}</c>", Name);
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

bool ContentNamespace::Contains(ContentObject const& object) const
{
    return object.Namespace && (object.Namespace == this || Contains(*object.Namespace));
}

bool ContentNamespace::MatchesFilter(ContentFilter& filter) const
{
    if (!filter.IsFilteringNamespaces())
        return true;

    auto& result = filter.FilteredNamespaces[Index];
    if (result == ContentFilter::UNCACHED_RESULT)
    {
        std::wstring displayName;
        result =
            !filter ||
            !filter.NameSearch.empty() && std::ranges::search(Name, filter.NameSearch, std::ranges::equal_to(), std::towupper, std::towupper) ||
            !filter.NameSearch.empty() && (displayName = GetDisplayName(false, true), std::ranges::search(displayName, filter.NameSearch, std::ranges::equal_to(), std::towupper, std::towupper)) ||
            std::ranges::any_of(Namespaces, std::bind_back(&ContentNamespace::MatchesFilter, std::ref(filter))) ||
            std::ranges::any_of(Entries, std::bind_back(&ContentObject::MatchesFilter, std::ref(filter)));
    }
    return result;
}

}
