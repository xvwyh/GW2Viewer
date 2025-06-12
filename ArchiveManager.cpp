#include "ArchiveManager.h"

#include "PackFile.h"
#include "Utils.h"

#include <Windows.h>

STATIC(g_archives);

std::vector<byte> ArchiveManager::GetFile(uint32 fileID)
{
    std::vector<byte> buffer;
    for (auto& source : m_sources)
    {
        if (auto const size = source.Archive.GetFileSize(fileID))
        {
            buffer.resize(size);
            source.Archive.GetFile(fileID, buffer);
            if (buffer.size() == size)
                break;
        }
    }
    return buffer;
}

std::unique_ptr<pf::PackFile> ArchiveManager::GetPackFile(uint32 fileID)
{
    for (auto& source : m_sources)
        if (auto file = source.Archive.GetPackFile(fileID))
            return file;

    return nullptr;
}

bool ArchiveManager::ContainsFile(uint32 fileID)
{
    if (fileID > m_files.rbegin()->ID)
        return false;

    return std::ranges::binary_search(m_files, fileID, { }, &ArchiveFile::ID);
}

void ArchiveManager::Add(ArchiveKind kind, std::filesystem::path path)
{
    std::wstring expanded(1024, L'\0');
    if (auto const length = ExpandEnvironmentStrings(path.c_str(), expanded.data(), expanded.size()))
        expanded.resize(length - 1);
    else
        return;
    m_sources.emplace_back(m_sources.size(), kind, expanded);
}

void ArchiveManager::Load(ProgressBarContext& progress)
{
    if (m_loaded)
        return;
    m_loaded = true;

    for (auto& source : m_sources)
    {
        source.Archive.Open(source.Path, progress);
        source.Files.assign_range(source.Archive.FileIdToMftEntry | std::views::keys | std::views::transform([&](uint32 fileID) { return ArchiveFile(fileID, source); }));
        m_files.insert_range(source.Files);
    }
}
