export module GW2Viewer.Data.Voice.Manager;
import GW2Viewer.Common;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Encryption.RC4;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.Utils.Async.ProgressBarContext;
import std;
import <cassert>;

export namespace GW2Viewer::Data::Voice
{

class Manager
{
public:
    void Load(Utils::Async::ProgressBarContext& progress);
    bool WipeCache(uint32 voiceID)
    {
        if (voiceID >= m_maxID)
            return false;

        bool result = false;
        for (auto& cache : m_statusCache | std::views::values)
            result |= cache.erase(voiceID);

        return result;
    }
    Encryption::Status GetStatus(uint32 voiceID, Language lang, std::optional<uint64> decryptionKey = { })
    {
        if (voiceID >= m_maxID)
            return Encryption::Status::Missing;

        auto& cache = m_statusCache[lang];
        if (auto const itr = cache.find(voiceID); itr != cache.end())
            return itr->second;

        return cache.emplace(voiceID, [=]
        {
            auto const data = Get(voiceID, lang);
            if (data.empty())
                return Encryption::Status::Missing;

            auto test = [](std::span<byte const> data)
            {
                if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFB)
                    return true;
                if (data.size() >= 4 && *(fcc const*)data.data() == fcc::OggS)
                    return true;
                return false;
            };

            if (test(data))
                return Encryption::Status::Unencrypted;

            if (decryptionKey)
            {
                std::vector encrypted { std::from_range, data };
                Encryption::RC4(Encryption::RC4::MakeKey(*decryptionKey)).Crypt(encrypted);

                if (test(encrypted))
                    return Encryption::Status::Decrypted;
            }

            return Encryption::Status::Encrypted;
        }()).first->second;
    }
    std::span<byte const> Get(uint32 voiceID, Language lang, std::optional<uint64> decryptionKey = { })
    {
        if (voiceID >= m_maxID)
            return { };

        uint32 const fileIndex = voiceID / m_voicesPerFile;
        uint32 const voiceIndex = voiceID % m_voicesPerFile;
        auto& files = m_packFiles[lang];
        if (files.empty())
        {
            if (auto const count = m_files[lang].size())
                files.resize(count);
            else
                return { };
        }

        auto& file = files[fileIndex];
        if (!file)
        {
            auto const archiveFile = m_files[lang][fileIndex];
            if (!archiveFile)
                return { };
            file = archiveFile->GetPackFile();
            if (!file)
                return { };
            assert(file->Header.HeaderSize == sizeof(file->Header));
        }

        if (auto const& field = file->QueryChunk(fcc::BKCK)["asndFile"][voiceIndex]["audioData[]"])
            if (auto const& audioData = ((Pack::PackFile const*)field.GetPointer())->QueryChunk(fcc::ASND)["audioData[]"])
                return { audioData.GetPointer(), audioData.GetArraySize() };

        return { };
    }

private:
    uint32 m_voicesPerFile = 10;
    uint32 m_maxID = 0;
    std::unordered_map<Language, std::vector<Archive::File const*>> m_files;
    std::unordered_map<Language, std::vector<std::unique_ptr<Pack::PackFile>>> m_packFiles;
    std::unordered_map<Language, std::unordered_map<uint32, Encryption::Status>> m_statusCache;
};

}
