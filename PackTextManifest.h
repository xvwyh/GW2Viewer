#pragma once
#include "Common.h"

#include <unordered_map>
#include <vector>

extern uint32 g_stringsPerFile;
extern uint32 g_maxStringID;
extern std::unordered_map<Language, std::vector<uint32>> g_stringsFileIDs;

void LoadTextPackManifest(class Archive& archive, class ProgressBarContext& progress);
