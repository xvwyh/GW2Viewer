#pragma once
#include "Common.h"

#include <span>

enum class EncryptionStatus;

bool WipeVoiceCache(uint32 voiceID);
EncryptionStatus GetVoiceStatus(uint32 voiceID, Language lang, std::optional<uint64> decryptionKey = { });
std::span<byte const> GetVoice(uint32 voiceID, Language lang, std::optional<uint64> decryptionKey = { });
