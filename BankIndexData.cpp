#include "BankIndexData.h"

#include "Archive.h"
#include "PackFileLayoutTraversal.h"
#include "ProgressBarContext.h"
#include "Utils.h"

#include <cassert>

STATIC(g_voicesPerFile) = 10;
STATIC(g_maxVoiceID) = 0;
STATIC(g_bankFileIDs);

void LoadBankIndexData(Archive& archive, ProgressBarContext& progress)
{
    progress.Start("Loading sound bank index");
    if (auto const file = archive.GetPackFile(184774))
    {
        if (uint32 const bankIndexFileID = file->QueryChunk(fcc::AMSP)["audioSettings"]["bankIndexFileName"])
        {
            if (auto const bankIndex = archive.GetPackFile(bankIndexFileID))
            {
                for (auto const& language : bankIndex->QueryChunk(fcc::BIDX)["bankLanguage"])
                {
                    std::ranges::transform(language["bankFileName"], std::back_inserter(g_bankFileIDs[(Language)language.GetArrayIndex()]), [](auto const& filename) -> uint32 { return filename["fileName"]; });
                    if (auto const maxID = g_voicesPerFile * language["bankFileName[]"].GetArraySize(); g_maxVoiceID < maxID)
                        g_maxVoiceID = maxID;
                }
            }
        }
    }
}
