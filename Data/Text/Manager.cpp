module GW2Viewer.Data.Text.Manager;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.User.Config;

namespace GW2Viewer::Data::Text
{

void Manager::Load(Utils::Async::ProgressBarContext& progress)
{
    progress.Start("Loading text manifest");
    if (auto const file = G::Game.Archive.GetPackFile(110865))
    {
        if (auto const manifest = file->QueryChunk(fcc::txtm))
        {
            m_stringsPerFile = manifest["stringsPerFile"];
            for (auto const& language : manifest["languages"])
            {
                std::ranges::copy(language["filenames"], std::back_inserter(m_fileIDs[(Language)language.GetArrayIndex()]));
                if (auto const maxID = m_stringsPerFile * language["filenames[]"].GetArraySize(); m_maxID < maxID)
                    m_maxID = maxID;
            }
        }
    }

    progress.Start("Loading text variants");
    if (auto const file = G::Game.Archive.GetPackFile(198298))
        for (auto const& variant : file->QueryChunk(fcc::vari)["variants"])
            std::ranges::copy(variant["variantTextIds"], std::back_inserter(m_variants[variant["textId"]]));

    progress.Start("Loading text voices");
    if (auto const file = G::Game.Archive.GetPackFile(198300))
        for (auto const& voice : file->QueryChunk(fcc::txtv)["voices"])
            m_voices.emplace(voice["textId"], voice["voiceId"]);
}
void Manager::LoadLanguage(Language language, Utils::Async::ProgressBarContext& progress)
{
    if (m_languageLoaded[language])
        return;
    m_languageLoaded[language] = true;

    auto const& fileIDs = m_fileIDs[language];
    progress.Start(std::format("Loading strings files: {}", language), fileIDs.size());
    m_stringsFiles[language].reserve(fileIDs.size());
    for (auto const [fileIndex, fileID] : fileIDs | std::views::enumerate)
    {
        m_stringsFiles[language].emplace_back(G::Game.Archive.GetFile(fileID), language, fileIndex, m_stringsPerFile);
        ++progress;
    }
}

Manager::StringsFile::TCache const& Manager::GetStringImpl(uint32 stringID)
{
    if (stringID >= m_maxID)
        return StringsFile::Missing;

    uint32 const fileIndex = stringID / m_stringsPerFile;
    uint32 const stringIndex = stringID % m_stringsPerFile;
    auto& files = m_stringsFiles[G::Config.Language];
    if (fileIndex >= files.size())
        return StringsFile::Missing;

    return files[fileIndex].Get(stringIndex, G::Game.Encryption.GetTextKey(stringID));
}

}
