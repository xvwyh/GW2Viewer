module;
#include "UI/ImGui/ImGui.h"
#include <ctime>

export module GW2Viewer.Data.Encryption.Text;
import GW2Viewer.Common;
import std;

export namespace Data::Encryption
{

struct TextKeyInfo
{
    uint64 Key { };
    time_t Time = _time64(nullptr);
    uint32 Session { };
    uint32 Map { };
    ImVec4 Position { };

    inline static uint32 NextIndex;
    uint32 Index = NextIndex++;
};

}
