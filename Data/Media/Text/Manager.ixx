export module GW2Viewer.Data.Media.Text.Manager;
import GW2Viewer.Common;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Encryption.RC4;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Async.ProgressBarContext;
import std;
import <boost/container/static_vector.hpp>;
import <boost/thread/locks.hpp>;
import <boost/thread/shared_mutex.hpp>;

export namespace GW2Viewer::Data::Media::Text
{

class Manager
{
public:
    bool IsLoaded(Language language) const { return m_stringsFiles.contains(language); }
    void Load(Archive::Source& source, Utils::Async::ProgressBarContext& progress)
    {
        progress.Start("Loading text manifest");
        if (auto const file = source.Archive.GetPackFile(110865))
        {
            if (auto const manifest = file->QueryChunk(fcc::txtm))
            {
                m_stringsPerFile = manifest["stringsPerFile"];
                for (auto const& language : manifest["languages"])
                {
                    std::ranges::copy(language["filenames"], std::back_inserter(m_fileIDs[(Language)language.GetArrayIndex()]));
                    if (auto const maxID = m_stringsPerFile * language["filenames[]"].GetArraySize(); m_maxID < maxID)
                        m_maxID = maxID;
                }
            }
        }

        progress.Start("Loading text variants");
        if (auto const file = source.Archive.GetPackFile(198298))
            for (auto const& variant : file->QueryChunk(fcc::vari)["variants"])
                std::ranges::copy(variant["variantTextIds"], std::back_inserter(m_variants[variant["textId"]]));

        progress.Start("Loading text voices");
        if (auto const file = source.Archive.GetPackFile(198300))
            for (auto const& voice : file->QueryChunk(fcc::txtv)["voices"])
                m_voices.emplace(voice["textId"], voice["voiceId"]);

        LoadLanguage(G::Config.Language, source, progress);
    }
    void LoadLanguage(Language language, Archive::Source& source, Utils::Async::ProgressBarContext& progress)
    {
        auto const& fileIDs = m_fileIDs[language];
        progress.Start("Loading strings files", fileIDs.size());
        m_stringsFiles[language].reserve(fileIDs.size());
        for (auto const [fileIndex, fileID] : fileIDs | std::views::enumerate)
        {
            m_stringsFiles[language].emplace_back(source.Archive.GetFile(fileID), language, fileIndex, m_stringsPerFile);
            ++progress;
        }
    }

    auto GetMaxID() const { return m_maxID; }
    bool WipeCache(uint32 stringID)
    {
        if (stringID >= m_maxID)
            return false;

        bool result = false;
        uint32 const fileIndex = stringID / m_stringsPerFile;
        uint32 const stringIndex = stringID % m_stringsPerFile;
        for (auto& files : m_stringsFiles | std::views::values)
            if (fileIndex < files.size())
                result |= files[fileIndex].Wipe(stringIndex);

        return result;
    }
    std::pair<std::wstring const*, Encryption::Status> Get(uint32 stringID)
    {
        auto const& [string, normalized, status] = GetStringImpl(stringID);
        switch (status)
        {
            using enum Encryption::Status;
            case Unencrypted:
            case Decrypted:
                return { &string, status };
            case Missing:
            case Encrypted:
            default:
                return { nullptr, status };
        }
    }
    std::pair<std::wstring const*, Encryption::Status> GetNormalized(uint32 stringID)
    {
        auto const& [string, normalized, status] = GetStringImpl(stringID);
        switch (status)
        {
            using enum Encryption::Status;
            case Unencrypted:
            case Decrypted:
                return { &normalized, status };
            case Missing:
            case Encrypted:
            default:
                return { nullptr, status };
        }
    }

    auto GetVariants(uint32 stringID) const
    {
        auto const itr = m_variants.find(stringID);
        return itr != m_variants.end() ? &itr->second : nullptr;
    }
    auto GetVoice(uint32 stringID) const
    {
        auto const itr = m_voices.find(stringID);
        return itr != m_voices.end() ? itr->second : 0;
    }

private:
    uint32 m_stringsPerFile = 0;
    uint32 m_maxID = 0;
    std::unordered_map<Language, std::vector<uint32>> m_fileIDs;
    std::unordered_map<uint32, boost::container::static_vector<uint32, (size_t)Race::Max * (size_t)Sex::None>> m_variants;
    std::unordered_map<uint32, uint32> m_voices;

    class StringsFile
    {
    public:
        using TCache = std::tuple<std::wstring, std::wstring, Encryption::Status>;

        StringsFile(std::vector<byte>&& data, Language lang, uint32 fileIndex, uint32 stringsPerFile) : Data(std::move(data))
        {
            if (*(fcc*)Data.data() != fcc::strs)
                return;
            Strings.resize(stringsPerFile);
            uint32 offset = sizeof(fcc);
            for (auto& string : Strings)
            {
                string = (Entry const*)&Data[offset];
                offset += string->header.bytes;
            }
            //assert(offset == Data.size() - 2);
            //assert((pf::Language&)Data[offset++] == lang);
            //assert(Data[offset] == fileIndex % std::numeric_limits<byte>().max());
        }
        StringsFile(StringsFile const& source) : Data(source.Data), Strings(Strings) { }
        StringsFile(StringsFile&& source) : Data(std::move(source.Data)), Strings(std::move(source.Strings)) { }

        inline static TCache const Missing { { }, { }, Encryption::Status::Missing };
        inline static TCache const Encrypted { { }, { }, Encryption::Status::Encrypted };

        TCache const& Get(uint32 stringIndex, std::optional<uint64> decryptionKey = { })
        {
            if (Strings.empty())
                return Missing;

            assert(stringIndex < Strings.size());
            boost::upgrade_lock readLock(CacheLock);
            auto itr = Cache.find(stringIndex);
            if (itr == Cache.end())
            {
                boost::upgrade_to_unique_lock _(readLock);
                auto const& entry = Strings[stringIndex];
                if (entry->IsEncrypted() && !decryptionKey)
                    return Encrypted;
                auto string = entry->Get(std::move(decryptionKey));
                std::wstring normalized(std::from_range, string | std::views::transform(toupper));
                itr = Cache.try_emplace(stringIndex, std::move(string), std::move(normalized), entry->IsEncrypted() ? Encryption::Status::Decrypted : Encryption::Status::Unencrypted).first;
            }
            return itr->second;
        }
        bool Wipe(uint32 stringIndex)
        {
            if (Strings.empty())
                return false;

            assert(stringIndex < Strings.size());
            boost::upgrade_lock readLock(CacheLock);
            if (auto const itr = Cache.find(stringIndex); itr != Cache.end())
            {
                boost::upgrade_to_unique_lock _(readLock);
                Cache.erase(itr);
                return true;
            }
            return false;
        }

    private:
        #pragma pack(push, 1)
        struct Entry
        {
            struct StringHeader
            {
                uint16 bytes;
                uint16 rangeBase; // Name unverified
                uint16 rangeBits;
            } header;
            union
            {
                byte EncryptedData[];
                wchar_t String[];
            };

            [[nodiscard]] bool IsEncrypted() const { return header.rangeBase || header.rangeBits != 8 * sizeof(wchar_t); }
            [[nodiscard]] std::wstring Get(std::optional<uint64> decryptionKey = { }) const
            {
                return IsEncrypted()
                    ? Decrypt(*decryptionKey)
                    : std::wstring(String, (header.bytes - offsetof(Entry, String)) / 2);
            }
            [[nodiscard]] std::wstring Decrypt(uint64 decryptionKey) const
            {
                return DecryptString({ EncryptedData, header.bytes - offsetof(Entry, EncryptedData) }, decryptionKey, header.rangeBase - 1, header.rangeBits);
            }
        };
        #pragma pack(pop)

        std::vector<byte> Data;
        std::vector<Entry const*> Strings;
        std::unordered_map<uint32, TCache> Cache;
        boost::shared_mutex CacheLock;
    };
    std::map<Language, std::vector<StringsFile>> m_stringsFiles;
    StringsFile::TCache const& GetStringImpl(uint32 stringID);
    static std::wstring DecryptString(std::span<byte const> const encryptedText, uint64 const key, uint16 const decryptionOffset, uint32 const bitsPerSymbol)
    {
        std::vector packedText(encryptedText.begin(), encryptedText.end());
        Encryption::RC4(Encryption::RC4::MakeKey(key)).Crypt(packedText);

        static constexpr std::wstring_view alphabet = L"0123456strnum()[]<>%#/:-'\" ,.!\n\0";
        uint32 accumulator = 0;
        uint32 bitsLeft = 0;
        std::wstring result(8 * packedText.size() / bitsPerSymbol, L'\0');
        for (auto itr = packedText.begin(); auto& c : result)
        {
            for (; bitsLeft <= 24; bitsLeft += 8)
                if (itr != packedText.end())
                    accumulator |= *itr++ << bitsLeft;

            uint32 const value = accumulator & ((1 << bitsPerSymbol) - 1);
            bitsLeft -= bitsPerSymbol;
            accumulator >>= bitsPerSymbol;

            if (!value)
                c = 0;
            else if (value - 1 < alphabet.size())
                c = alphabet[value - 1];
            else
                c = value + decryptionOffset - (uint32)alphabet.size();
        }
        if (auto const trim = result.find_last_not_of(L'\0'); trim != std::wstring::npos && trim + 1 != result.size())
            result.resize(trim + 1);
        return result;
    }
};

}
