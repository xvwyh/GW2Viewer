export module GW2Viewer.Common.Token;
import GW2Viewer.Common;
import std;

#pragma pack(push, 8)
export struct Token
{
    enum Types
    {
        TypeNone,
        TypeFloat,
        TypeUInt,
    } Type;
    union
    {
        float Float;
        uint64 UInt;
    };
};
#pragma pack(pop)
export Token& operator+=(Token& a, Token b)
{
    if (a.Type == Token::TypeFloat)
    {
        a.Float += b.Float;
    }
    else if (a.Type == Token::TypeUInt)
    {
        if (b.Type == Token::TypeUInt)
        {
            uint64 token = (a.UInt & 0xFFFFFFFFFFFFFFF) << 4;
            int length = 12;
            do
            {
                if (token & 0xF800000000000000)
                    break;
                token <<= 5;
                --length;
            }
            while (length);
            a.UInt = (b.UInt & 0xFFFFFFFFFFFFFFF) << 5 * (byte)length & 0xFFFFFFFFFFFFFFF | a.UInt & 0xFFFFFFFFFFFFFFF | (uint64)std::clamp((uint32)((a.UInt >> 60) + (b.UInt >> 60)), 0u, 15u) << 60;
        }
        else if (b.Type == Token::TypeFloat)
        {
            a.UInt = a.UInt & 0xFFFFFFFFFFFFFFF | (uint64)std::clamp((int)((int)b.Float + (a.UInt >> 60)), 0, 15) << 60;
        }
    }
    return a;
}
