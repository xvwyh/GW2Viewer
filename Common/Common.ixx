export module GW2Viewer.Common;
import std;

export namespace GW2Viewer
{

using byte = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using sbyte = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

enum class Language : byte
{
    English,
    Korean,
    French,
    German,
    Spanish,
    Chinese,
};

enum class Race
{
    Asura,
    Charr,
    Human,
    Norn,
    Sylvari,
    Max
};

enum class Sex
{
    Male,
    Female,
    None,
    Max
};

}
