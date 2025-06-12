#pragma once
#include "Common.h"

extern uint32 g_firstContentFileID;
extern uint32 g_numContentFiles;

void LoadContentFiles(class Archive& archive, class ProgressBarContext& progress);
void ProcessContentFiles(ProgressBarContext& progress);
