#pragma once
#include "Common.h"

#include <unordered_map>

extern std::unordered_map<uint32, uint32> g_textVoices;

void LoadTextPackVoices(class Archive& archive, class ProgressBarContext& progress);
