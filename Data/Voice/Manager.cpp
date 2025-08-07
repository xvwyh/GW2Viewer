module GW2Viewer.Data.Voice.Manager;
import GW2Viewer.Data.Game;

namespace GW2Viewer::Data::Voice
{

void Manager::Load(Utils::Async::ProgressBarContext& progress)
{
    progress.Start("Loading sound bank index");
    if (auto const file = G::Game.Archive.GetPackFile(184774))
    {
        if (uint32 const bankIndexFileID = file->QueryChunk(fcc::AMSP)["audioSettings"]["bankIndexFileName"])
        {
            if (auto const bankIndex = G::Game.Archive.GetPackFile(bankIndexFileID))
            {
                for (auto const& language : bankIndex->QueryChunk(fcc::BIDX)["bankLanguage"])
                {
                    std::ranges::transform(language["bankFileName"], std::back_inserter(m_files[(Language)language.GetArrayIndex()]), [](auto const& filename) { return G::Game.Archive.GetFileEntry(filename["fileName"]); });
                    if (auto const maxID = m_voicesPerFile * language["bankFileName[]"].GetArraySize(); m_maxID < maxID)
                        m_maxID = maxID;
                }
            }
        }
    }
}

}
