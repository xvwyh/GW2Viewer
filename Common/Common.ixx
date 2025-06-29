export module GW2Viewer.Common;
import std;

export using byte = std::uint8_t;
export using uint16 = std::uint16_t;
export using uint32 = std::uint32_t;
export using uint64 = std::uint64_t;
export using sbyte = std::int8_t;
export using int16 = std::int16_t;
export using int32 = std::int32_t;
export using int64 = std::int64_t;

export enum class Language : byte
{
    English,
    Korean,
    French,
    German,
    Spanish,
    Chinese,
};

export enum class Race
{
    Asura,
    Charr,
    Human,
    Norn,
    Sylvari,
    Max
};

export enum class Sex
{
    Male,
    Female,
    None,
    Max
};
