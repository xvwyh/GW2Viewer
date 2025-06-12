#include "Encryption.h"

#include "IconsFontAwesome6.h"
#include "StringsFile.h"

#include <map>
#include <shared_mutex>
#include <unordered_map>

STATIC(decryptionKeysLock);
std::unordered_map<uint32, StringDecryptionKey> stringDecryptionKeys;
std::vector<StringDecryptionKey*> stringDecryptionKeysByOrder;
std::map<std::pair<EncryptedAssetType, uint32>, uint64> assertDecryptionKeys;

StringDecryptionKey* AddDecryptionKeyInfo(uint32 stringID, StringDecryptionKey info)
{
    return stringDecryptionKeysByOrder.emplace_back(&(stringDecryptionKeys[stringID] = std::move(info)));
}

StringDecryptionKey* GetDecryptionKeyInfo(uint32 stringID)
{
    std::shared_lock _(decryptionKeysLock);
    if (auto const itr = stringDecryptionKeys.find(stringID); itr != stringDecryptionKeys.end())
        return &itr->second;
    return nullptr;
}
std::optional<uint64> GetDecryptionKey(uint32 stringID)
{
    if (auto const info = GetDecryptionKeyInfo(stringID); info && info->Key)
        return info->Key;
    return { };
}

std::unordered_map<EncryptionStatus, std::pair<char const*, char const*>> const encryptionStatuses
{
    { EncryptionStatus::Missing,     { "F00", "<nosel><c=#F00>" ICON_FA_BAN "</c></nosel>" } },
    { EncryptionStatus::Unencrypted, { "FFF", "" } },
    { EncryptionStatus::Encrypted,   { "F80", "<nosel>" ICON_FA_KEY "</nosel>" } },
    { EncryptionStatus::Decrypted,   { "0F0", "<nosel><c=#4>" ICON_FA_KEY "</c> </nosel>"} },
};
char const* GetEncryptionStatusColor(EncryptionStatus status)
{
    return encryptionStatuses.at(status).first;
}
char const* GetEncryptionStatusText(EncryptionStatus status)
{
    return encryptionStatuses.at(status).second;
}

void AddDecryptionKey(EncryptedAssetType assetType, uint32 assetID, uint64 key)
{
    assertDecryptionKeys[{ assetType, assetID }] = key;
}

std::optional<uint64> GetDecryptionKey(EncryptedAssetType assetType, uint32 assetID)
{
    std::shared_lock _(decryptionKeysLock);
    if (auto const itr = assertDecryptionKeys.find({ assetType, assetID }); itr != assertDecryptionKeys.end())
        return itr->second;
    return { };
}
