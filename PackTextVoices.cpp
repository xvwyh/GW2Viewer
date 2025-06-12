#include "PackTextVoices.h"

#include "Archive.h"
#include "PackFileLayoutTraversal.h"
#include "ProgressBarContext.h"
#include "Utils.h"

STATIC(g_textVoices) { };

void LoadTextPackVoices(Archive& archive, ProgressBarContext& progress)
{
    progress.Start("Loading text voices");
    if (auto const file = archive.GetPackFile(198300))
        for (auto const& voice : file->QueryChunk(fcc::txtv)["voices"])
            g_textVoices.emplace(voice["textId"], voice["voiceId"]);
}
