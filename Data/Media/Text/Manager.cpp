module GW2Viewer.Data.Media.Text.Manager;
import GW2Viewer.Data.Game;

Data::Media::Text::Manager::StringsFile::TCache const& Data::Media::Text::Manager::GetStringImpl(uint32 stringID)
{
    if (stringID >= m_maxID)
        return StringsFile::Missing;

    uint32 const fileIndex = stringID / m_stringsPerFile;
    uint32 const stringIndex = stringID % m_stringsPerFile;
    auto& files = g_stringsFiles[G::Config.Language];
    if (fileIndex >= files.size())
        return StringsFile::Missing;

    return files[fileIndex].Get(stringIndex, G::Game.Encryption.GetTextKey(stringID));
}
