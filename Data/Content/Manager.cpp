module GW2Viewer.Data.Content.Manager;
import GW2Viewer.Data.Game;

namespace GW2Viewer::Data::Content
{

void Manager::Load(Utils::Async::ProgressBarContext& progress)
{
    m_loadedContentFiles.resize(m_numContentFiles);
    uint32 fileID = m_firstContentFileID;
    progress.Start("Loading content files", m_loadedContentFiles.size());
    for (auto& loaded : m_loadedContentFiles)
    {
        loaded.File = G::Game.Archive.GetPackFile(fileID++);
        ++progress;
    }

    Process(progress);
}

}
