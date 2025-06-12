#pragma once
#include "Common.h"

#include <imgui.h>

#include <shared_mutex>

enum class EncryptionStatus
{
    Missing,
    Unencrypted,
    Encrypted,
    Decrypted,
};

struct StringDecryptionKey
{
    uint64 Key { };
    time_t Time = time(nullptr);
    uint32 Session { };
    uint32 Map { };
    ImVec4 Position { };

    inline static uint32 NextIndex;
    uint32 Index = NextIndex++;
};
StringDecryptionKey* AddDecryptionKeyInfo(uint32 stringID, StringDecryptionKey info);
StringDecryptionKey* GetDecryptionKeyInfo(uint32 stringID);
std::optional<uint64> GetDecryptionKey(uint32 stringID);
char const* GetEncryptionStatusColor(EncryptionStatus status);
char const* GetEncryptionStatusText(EncryptionStatus status);

enum class EncryptedAssetType
{
    Voice = 1,
};
void AddDecryptionKey(EncryptedAssetType assetType, uint32 assetID, uint64 key);
std::optional<uint64> GetDecryptionKey(EncryptedAssetType assetType, uint32 assetID);

extern std::shared_mutex decryptionKeysLock;
