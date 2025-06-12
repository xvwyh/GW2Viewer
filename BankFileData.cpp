#include "BankFileData.h"

#include "ArchiveManager.h"
#include "BankIndexData.h"
#include "Encryption.h"
#include "PackFile.h"
#include "PackFileLayoutTraversal.h"
#include "RC4.h"

#include <memory>
#include <unordered_map>

std::unordered_map<Language, std::vector<std::unique_ptr<pf::PackFile>>> g_voiceFiles;
std::unordered_map<Language, std::unordered_map<uint32, EncryptionStatus>> g_voiceStatus;

bool WipeVoiceCache(uint32 voiceID)
{
    if (voiceID >= g_maxVoiceID)
        return false;

    bool result = false;
    for (auto& cache : g_voiceStatus | std::views::values)
        result |= cache.erase(voiceID);

    return result;
}

EncryptionStatus GetVoiceStatus(uint32 voiceID, Language lang, std::optional<uint64> decryptionKey)
{
    if (voiceID >= g_maxVoiceID)
        return EncryptionStatus::Missing;

    auto& cache = g_voiceStatus[lang];
    if (auto const itr = cache.find(voiceID); itr != cache.end())
        return itr->second;

    return cache.emplace(voiceID, [=]
    {
        auto const data = GetVoice(voiceID, lang);
        if (data.empty())
            return EncryptionStatus::Missing;

        auto test = [](std::span<byte const> data)
        {
            if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFB)
                return true;
            if (data.size() >= 4 && *(fcc const*)data.data() == fcc::OggS)
                return true;
            return false;
        };

        if (test(data))
            return EncryptionStatus::Unencrypted;

        std::vector encrypted { std::from_range, data };
        RC4(RC4::MakeKey(*decryptionKey)).Crypt(encrypted);

        if (test(encrypted))
            return EncryptionStatus::Decrypted;

        return EncryptionStatus::Encrypted;
    }()).first->second;
}

std::span<byte const> GetVoice(uint32 voiceID, Language lang, std::optional<uint64> decryptionKey)
{
    if (voiceID >= g_maxVoiceID)
        return { };

    uint32 const fileIndex = voiceID / g_voicesPerFile;
    uint32 const voiceIndex = voiceID % g_voicesPerFile;
    auto& files = g_voiceFiles[lang];
    if (files.empty())
    {
        if (auto const count = g_bankFileIDs[lang].size())
            files.resize(count);
        else
            return { };
    }

    auto& file = files[fileIndex];
    if (!file)
    {
        file = g_archives.GetPackFile(g_bankFileIDs[lang][fileIndex]);
        if (!file)
            return { };
        assert(file->Header.HeaderSize == sizeof(file->Header));
    }

    if (auto const& field = file->QueryChunk(fcc::BKCK)["asndFile"][voiceIndex]["audioData[]"])
        if (auto const& audioData = ((pf::PackFile const*)field.GetPointer())->QueryChunk(fcc::ASND)["audioData[]"])
            return { audioData.GetPointer(), audioData.GetArraySize() };

    return { };
}
