export module GW2Viewer.Data.Game;
import GW2Viewer.Common;
import GW2Viewer.Data.Archive.Manager;
import GW2Viewer.Data.Content.Manager;
import GW2Viewer.Data.Encryption.Manager;
import GW2Viewer.Data.Manifest.Manager;
import GW2Viewer.Data.Pack.Manager;
import GW2Viewer.Data.Text.Manager;
import GW2Viewer.Data.Texture.Manager;
import GW2Viewer.Data.Voice.Manager;
import GW2Viewer.Utils.Async.ProgressBarContext;
import std;

export namespace GW2Viewer::Data
{

struct Game
{
    uint32 Build = 0;
    std::set<uint32> ReferencedFiles;

    Archive::Manager Archive;
    Content::Manager Content;
    Encryption::Manager Encryption;
    Manifest::Manager Manifest;
    Pack::Manager Pack;
    Text::Manager Text;
    Texture::Manager Texture;
    Voice::Manager Voice;

    void Load(std::filesystem::path const& path, Utils::Async::ProgressBarContext& progress);
};

}

export namespace GW2Viewer::G { Data::Game Game; }
