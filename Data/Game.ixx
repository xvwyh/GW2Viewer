export module GW2Viewer.Data.Game;
import GW2Viewer.Common;
import GW2Viewer.Data.Archive.Manager;
import GW2Viewer.Data.Content.Manager;
import GW2Viewer.Data.Encryption.Manager;
import GW2Viewer.Data.Media.Text.Manager;
import GW2Viewer.Data.Media.Texture.Manager;
import GW2Viewer.Data.Media.Voice.Manager;
import GW2Viewer.Data.Pack.Manager;
import GW2Viewer.Utils.Async.ProgressBarContext;
import std;

export namespace GW2Viewer::Data
{

struct Game
{
    uint32 Build = 0;

    Archive::Manager Archive;
    Content::Manager Content;
    Encryption::Manager Encryption;
    Pack::Manager Pack;
    Media::Text::Manager Text;
    Media::Texture::Manager Texture;
    Media::Voice::Manager Voice;

    void Load(std::filesystem::path const& path, Utils::Async::ProgressBarContext& progress);
};

}

export namespace GW2Viewer::G { Data::Game Game; }
