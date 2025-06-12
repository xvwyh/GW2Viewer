#pragma once
#include "Common.h"

#include <unordered_map>

extern uint32 g_voicesPerFile;
extern uint32 g_maxVoiceID;
extern std::unordered_map<Language, std::vector<uint32>> g_bankFileIDs;

void LoadBankIndexData(class Archive& archive, class ProgressBarContext& progress);
