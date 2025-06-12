#include "PackTextManifest.h"

#include "Archive.h"
#include "PackFileLayoutTraversal.h"
#include "ProgressBarContext.h"
#include "Utils.h"

#include <algorithm>
#include <cassert>

STATIC(g_stringsPerFile) = 0;
STATIC(g_maxStringID) = 0;
STATIC(g_stringsFileIDs);

void LoadTextPackManifest(Archive& archive, ProgressBarContext& progress)
{
    progress.Start("Loading text manifest");
    if (auto const file = archive.GetPackFile(110865))
    {
        if (auto const manifest = file->QueryChunk(fcc::txtm))
        {
            g_stringsPerFile = manifest["stringsPerFile"];
            for (auto const& language : manifest["languages"])
            {
                std::ranges::copy(language["filenames"], std::back_inserter(g_stringsFileIDs[(Language)language.GetArrayIndex()]));
                if (auto const maxID = g_stringsPerFile * language["filenames[]"].GetArraySize(); g_maxStringID < maxID)
                    g_maxStringID = maxID;
            }
        }
    }
}
