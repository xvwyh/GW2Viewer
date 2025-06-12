#include "PackTextVariants.h"

#include "Archive.h"
#include "PackFileLayoutTraversal.h"
#include "ProgressBarContext.h"
#include "Utils.h"

#include <cassert>

STATIC(g_textVariants);

void LoadTextPackVariants(Archive& archive, ProgressBarContext& progress)
{
    progress.Start("Loading text variants");
    if (auto const file = archive.GetPackFile(198298))
        for (auto const& variant : file->QueryChunk(fcc::vari)["variants"])
            std::ranges::copy(variant["variantTextIds"], std::back_inserter(g_textVariants[variant["textId"]]));
}
