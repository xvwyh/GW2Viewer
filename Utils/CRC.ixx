export module GW2Viewer.Utils.CRC;
import GW2Viewer.Common;
import std;
import <crc32c/crc32c.h>;

export namespace GW2Viewer::Utils::CRC
{

uint32 Calculate(uint32 seed, std::span<byte const> data)
{
    return crc32c_extend(seed, data.data(), data.size());
}

}
