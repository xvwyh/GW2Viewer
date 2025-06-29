module;
#include <Windows.h>

module GW2Viewer.Data.Archive.Manager;

void Data::Archive::Manager::Add(Kind kind, std::filesystem::path const& path)
{
    std::wstring expanded(1024, L'\0');
    if (auto const length = ExpandEnvironmentStrings(path.c_str(), expanded.data(), expanded.size()))
        expanded.resize(length - 1);
    else
        return;
    m_sources.emplace_back(m_sources.size(), kind, expanded);
}
