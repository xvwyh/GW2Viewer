#include "StringsFile.h"

#include "Archive.h"
#include "Config.h"
#include "Encryption.h"
#include "PackTextManifest.h"
#include "ProgressBarContext.h"
#include "RC4.h"

#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

std::wstring DecryptString(std::span<uint8_t const> const encryptedText, uint64_t const key, uint16_t const decryptionOffset, uint32_t const bitsPerSymbol)
{
    std::vector packedText(encryptedText.begin(), encryptedText.end());
    RC4(RC4::MakeKey(key)).Crypt(packedText);

    static constexpr std::wstring_view alphabet = L"0123456strnum()[]<>%#/:-'\" ,.!\n\0";
    uint32_t accumulator = 0;
    uint32_t bitsLeft = 0;
    std::wstring result(8 * packedText.size() / bitsPerSymbol, L'\0');
    for (auto itr = packedText.begin(); auto& c : result)
    {
        for (; bitsLeft <= 24; bitsLeft += 8)
            if (itr != packedText.end())
                accumulator |= *itr++ << bitsLeft;

        uint32_t const value = accumulator & ((1 << bitsPerSymbol) - 1);
        bitsLeft -= bitsPerSymbol;
        accumulator >>= bitsPerSymbol;

        if (!value)
            c = 0;
        else if (value - 1 < alphabet.size())
            c = alphabet[value - 1];
        else
            c = value + decryptionOffset - (uint32_t)alphabet.size();
    }
    if (auto const trim = result.find_last_not_of(L'\0'); trim != std::wstring::npos && trim + 1 != result.size())
        result.resize(trim + 1);
    return result;
}

class StringsFile
{
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

        [[nodiscard]] bool IsEncrypted() const { return header.rangeBase && header.rangeBits != 8 * sizeof(wchar_t); }
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

    using TCache = std::tuple<std::wstring, std::wstring, EncryptionStatus>;

    std::vector<byte> Data;
    std::vector<Entry const*> Strings;
    std::unordered_map<uint32, TCache> Cache;
    boost::shared_mutex CacheLock;

public:
    StringsFile(std::vector<byte>&& data, Language lang, uint32 fileIndex) : Data(std::move(data))
    {
        if (*(fcc*)Data.data() != fcc::strs)
            return;
        Strings.resize(g_stringsPerFile);
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

    inline static TCache const Missing { { }, { }, EncryptionStatus::Missing };
    inline static TCache const Encrypted { { }, { }, EncryptionStatus::Encrypted };

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
            itr = Cache.try_emplace(stringIndex, std::move(string), std::move(normalized), entry->IsEncrypted() ? EncryptionStatus::Decrypted : EncryptionStatus::Unencrypted).first;
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
};

//STATIC(StringsFile::Missing) { { }, { }, StringStatus::Missing };
//STATIC(StringsFile::Encrypted) { { }, { }, StringStatus::Encrypted };

std::map<Language, std::vector<StringsFile>> g_stringsFiles;

void LoadStringsFiles(Archive& archive, ProgressBarContext& progress)
{
    progress.Start("Loading strings files", std::accumulate(g_stringsFileIDs.begin(), g_stringsFileIDs.end(), 0, [](uint32 count, auto& pair) { return count + pair.second.size(); }));
    for (auto const& [language, fileIDs] : g_stringsFileIDs)
    {
        // TODO: Temporary solution to speed up debugging
        if (language != Language::English)
            continue;

        g_stringsFiles[language].reserve(fileIDs.size());
        for (auto const [fileIndex, fileID] : fileIDs | std::views::enumerate)
        {
            g_stringsFiles[language].emplace_back(archive.GetFile(fileID), language, fileIndex);
            ++progress;
        }
    }
}

bool WipeStringCache(uint32 stringID)
{
    if (stringID >= g_maxStringID)
        return false;

    bool result = false;
    uint32 const fileIndex = stringID / g_stringsPerFile;
    uint32 const stringIndex = stringID % g_stringsPerFile;
    for (auto& files : g_stringsFiles | std::views::values)
        if (fileIndex < files.size())
            result |= files[fileIndex].Wipe(stringIndex);

    return result;
}

auto& GetStringImpl(uint32 stringID)
{
    if (stringID >= g_maxStringID)
        return StringsFile::Missing;

    uint32 const fileIndex = stringID / g_stringsPerFile;
    uint32 const stringIndex = stringID % g_stringsPerFile;
    auto& files = g_stringsFiles[g_config.Language];
    if (fileIndex >= files.size())
        return StringsFile::Missing;

    return files[fileIndex].Get(stringIndex, GetDecryptionKey(stringID));
}
std::pair<std::wstring const*, EncryptionStatus> GetString(uint32 stringID)
{
    auto const& [string, normalized, status] = GetStringImpl(stringID);
    switch (status)
    {
        using enum EncryptionStatus;
        case Unencrypted:
        case Decrypted:
            return { &string, status };
        case Missing:
        case Encrypted:
        default:
            return { nullptr, status };
    }
}
std::pair<std::wstring const*, EncryptionStatus> GetNormalizedString(uint32 stringID)
{
    auto const& [string, normalized, status] = GetStringImpl(stringID);
    switch (status)
    {
        using enum EncryptionStatus;
        case Unencrypted:
        case Decrypted:
            return { &normalized, status };
        case Missing:
        case Encrypted:
        default:
            return { nullptr, status };
    }
}

void detail::FixupString(std::wstring& string)
{
    if (!string.contains(L'['))
        return;

    std::wstring result;
    result.resize(string.size());
    auto writeDest = result.data();
    uint32 sex = 2;
    bool plural = false;
    auto const pStart = string.data();
    auto const pEnd = pStart + string.size();
    auto copyFrom = pStart;
    auto p = pStart;
    auto write = [&]
    {
        if (copyFrom != p)
        {
            writeDest += string.copy(writeDest, p - copyFrom, copyFrom - pStart);
            copyFrom = p;
        }
    };
    for (; p < pEnd; ++p)
    {
        auto const c = *p;
        if (c == L'[')
        {
            write();
            auto const fixupEnd = string.find(']', std::distance(pStart, p + 1));
            if (fixupEnd == std::wstring::npos)
                break;
            std::wstring_view fixup { p + 1, (size_t)std::distance(p + 1, &string[fixupEnd]) };
            if (fixup == L"null")
                ;
            else if (fixup == L"lbracket")
                *writeDest++ = L'[';
            else if (fixup == L"rbracket")
                *writeDest++ = L']';
            else if (fixup == L"plur")
                plural = true;
            else if (fixup == L"m")
                sex = 0;
            else if (fixup == L"f")
                sex = 1;
            else if (fixup == L"s")
            {
                if (plural)
                    *writeDest++ = L's';
            }
            else if (fixup.starts_with(L"pl:\"") && fixup.ends_with('"'))
            {
                if (plural)
                {
                    writeDest = &result[std::wstring_view { result.data(), writeDest }.find_last_of(L' ') + 1];
                    std::fill(writeDest, result.data() + result.size(), L'\0');
                    writeDest += fixup.copy(writeDest, fixup.size() - 5, 4);
                }
            }
            else if (fixup.starts_with(L"f:\"") && fixup.ends_with('"'))
            {
                if (sex == 1)
                {
                    writeDest = &result[std::wstring_view { result.data(), writeDest }.find_last_of(L' ') + 1];
                    std::fill(writeDest, result.data() + result.size(), L'\0');
                    writeDest += fixup.copy(writeDest, fixup.size() - 5, 4);
                }
            }
            // an
            // the
            // nosep
            // b
            else
                continue;

            p = &string[fixupEnd];
            copyFrom = p + 1;
        }
    }
    write();

    result.resize(std::distance(result.data(), writeDest));
    string = std::move(result);
}
