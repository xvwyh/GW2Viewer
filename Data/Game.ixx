export module GW2Viewer.Data.Game;
import GW2Viewer.Data.Archive.Manager;
import GW2Viewer.Data.Content.Manager;
import GW2Viewer.Data.Encryption.Manager;
import GW2Viewer.Data.Media.Text.Manager;
import GW2Viewer.Data.Media.Texture.Manager;
import GW2Viewer.Data.Media.Voice.Manager;
import GW2Viewer.Data.Pack.Manager;
import std;

export namespace GW2Viewer::Data
{

struct Game
{
    /*
    void Load(ProgressBarContext& progress)
    {
        Archive.Load(progress);
        Pack.Load(, progress);
        if (auto const source = Archive.GetSource())
        {
            Text.Load(*source, progress);
            Voice.Load(*source, progress);
            Content.Load(*source, progress);
        }
    }
    */

    Archive::Manager Archive;
    Content::Manager Content;
    Encryption::Manager Encryption;
    Pack::Manager Pack;
    Media::Text::Manager Text;
    Media::Texture::Manager Texture;
    Media::Voice::Manager Voice;
};

}

export namespace GW2Viewer::G { Data::Game Game; }
