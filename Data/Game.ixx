export module GW2Viewer.Data.Game;
import GW2Viewer.Data.Archive.Manager;
import GW2Viewer.Data.Content.Manager;
import GW2Viewer.Data.Encryption.Manager;
import GW2Viewer.Data.Media.Text.Manager;
import GW2Viewer.Data.Media.Texture.Manager;
import GW2Viewer.Data.Media.Voice.Manager;
import GW2Viewer.Data.Pack.Manager;
import std;
import <cassert>;
import GW2Viewer.Common.GUID;

export namespace Data
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

    static_assert(std::is_same_v<GUID, MyGUID>);
    Archive::Manager Archive;
    static_assert(std::is_same_v<GUID, MyGUID>);
    Content::Manager Content;
    static_assert(std::is_same_v<GUID, MyGUID>);
    Encryption::Manager Encryption;
    static_assert(std::is_same_v<GUID, MyGUID>);
    Pack::Manager Pack;
    static_assert(std::is_same_v<GUID, MyGUID>);
    Media::Text::Manager Text;
    static_assert(std::is_same_v<GUID, MyGUID>);
    Media::Texture::Manager Texture;
    static_assert(std::is_same_v<GUID, MyGUID>);
    Media::Voice::Manager Voice;
    static_assert(std::is_same_v<GUID, MyGUID>);
};

}

export namespace G { Data::Game Game; }
