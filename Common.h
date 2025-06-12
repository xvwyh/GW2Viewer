#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <exception>
#include <string_view>
#include <boost/container/small_vector.hpp>

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

#pragma pack(push, 1)
namespace pf
{
template<typename T, typename PointerType>
class PtrBase
{
    PointerType m_offset;

public:
    [[nodiscard]] T* get() const { return m_offset ? (T*)((byte*)this + m_offset) : nullptr; }
    [[nodiscard]] T& value() const { return *get(); }

    [[nodiscard]] T& operator*() const { return *get(); }
    [[nodiscard]] T* operator->() const { return get(); }
    [[nodiscard]] operator bool() const { return m_offset; }
};
template<typename T> using Ptr32 = PtrBase<T, int32>;
template<typename T> using Ptr64 = PtrBase<T, int64>;
template<typename T> using Ptr = Ptr64<T>;

template<typename CharT, typename PointerType>
class StringBase
{
    PtrBase<CharT, PointerType> m_pointer;

public:
    [[nodiscard]] bool empty() const { return m_pointer; }
    [[nodiscard]] CharT* data() const { return m_pointer.get(); }

    [[nodiscard]] operator bool() const { return empty(); }
};
template<typename PointerType = int64> using String = StringBase<char, PointerType>;
template<typename PointerType = int64> using WString = StringBase<wchar_t, PointerType>;
using String32 = String<int32>;
using String64 = String<int64>;
using WString32 = WString<int32>;
using WString64 = WString<int64>;

template<typename T, typename SizeType, typename PointerType>
class ArrayBase
{
    SizeType m_count;
    PtrBase<T, PointerType> m_pointer;

public:
    [[nodiscard]] SizeType size() const { return m_count; }
    [[nodiscard]] bool empty() const { return size() == 0; }
    [[nodiscard]] T* data() const { return m_pointer.get(); }
    [[nodiscard]] T* begin() const { return data(); }
    [[nodiscard]] T& front() const { return *begin(); }
    [[nodiscard]] T& back() const { return *(end() - 1); }
    [[nodiscard]] T* end() const { return begin() + size(); }
    [[nodiscard]] T& at(SizeType index) const { if (index >= size()) throw "index out of range"; return (*this)[index]; }

    [[nodiscard]] T& operator[](SizeType index) const { return *(begin() + index); }
};
template<typename T> using DwordArray32 = ArrayBase<T, uint32, int32>;
template<typename T> using DwordArray64 = ArrayBase<T, uint32, int64>;
template<typename T> using WordArray32 = ArrayBase<T, uint16, int32>;
template<typename T> using WordArray64 = ArrayBase<T, uint16, int64>;
template<typename T> using ByteArray32 = ArrayBase<T, byte, int32>;
template<typename T> using ByteArray64 = ArrayBase<T, byte, int64>;
template<typename T> using Array32 = DwordArray32<T>;
template<typename T> using Array64 = DwordArray64<T>;
template<typename T> using Array = Array64<T>;

template<typename SizeType, typename PointerType>
class TypedArrayBase
{
    SizeType m_count;
    PtrBase<byte /* unk */, PointerType> m_types;
    PtrBase<byte, PointerType> m_pointer;

public:
    [[nodiscard]] SizeType size() const { return m_count; }
    [[nodiscard]] bool empty() const { return size() == 0; }
    [[nodiscard]] byte* types() const { return m_types.get(); }
    [[nodiscard]] byte* data() const { return m_pointer.get(); }
};
template<typename T> using DwordTypedArray32 = TypedArrayBase<uint32, int32>;
template<typename T> using DwordTypedArray64 = TypedArrayBase<uint32, int64>;
template<typename T> using WordTypedArray32 = TypedArrayBase<uint16, int32>;
template<typename T> using WordTypedArray64 = TypedArrayBase<uint16, int64>;
template<typename T> using ByteTypedArray32 = TypedArrayBase<byte, int32>;
template<typename T> using ByteTypedArray64 = TypedArrayBase<byte, int64>;

class FileReference // actually should be a wchar_t[]
{
    uint16 m_low;
    uint16 m_high;
    uint16 m_term;

public:
    [[nodiscard]] uint32 GetFileID() const { return !m_term && m_high >= 0x100 && m_low >= 0x100 ? (m_high - 0x100) * 0xff00 + (m_low - 0xff) : 0; }
};
template<typename PointerType = int64>
class FileNameBase
{
    PtrBase<FileReference, PointerType> m_reference;

public:
    [[nodiscard]] uint32 GetFileID() const { return m_reference ? m_reference->GetFileID() : 0; }
};
using FileName = FileNameBase<int64>;
using FileName32 = FileNameBase<int32>;

template<typename PointerType = int64>
class Variant
{
    uint32 m_index;
    PtrBase<byte, PointerType> m_pointer;

public:
    [[nodiscard]] uint32 index() const { return m_index; }
    [[nodiscard]] byte* data() const { return m_pointer.get(); }
};

class Token32
{
    static constexpr std::string_view alphabet = "abcdefghiklmnopvrstuwxy";
    uint32 m_data;

public:
    Token32() { }
    Token32(uint32 data) : m_data(data) { }
    Token32(std::string_view string) : Token32(FromString(string)) { }
    Token32(char const* string) : Token32(std::string_view(string)) { }

    [[nodiscard]] bool empty() const { return !m_data; }
    [[nodiscard]] auto GetString() const
    {
        boost::container::small_vector<char, 16> decoded;
        if (uint32 token = m_data)
        {
            if (token -= 0x30000000)
            {
                while (token)
                {
                    decoded.emplace_back(alphabet[token % 23]);
                    token /= 23;
                }
            }
        }
        decoded.emplace_back('\0');
        return decoded;
    }
    [[nodiscard]] static uint32 FromString(std::string_view string)
    {
        uint32 token = 0;
        uint32 factor = 1;

        for (char const c : string)
        {
            if (auto const pos = alphabet.find(c); pos != std::string_view::npos)
                token += pos * factor;
            else
                return 0;

            factor *= 23;
        }

        return token + 0x30000000;
    }
};

class Token64
{
    uint64 m_data;

public:
    Token64() { }
    Token64(uint64 data) : m_data(data) { }
    Token64(std::string_view string) : Token64(FromString(string)) { }
    Token64(char const* string) : Token64(std::string_view(string)) { }

    [[nodiscard]] bool empty() const { return !m_data; }
    [[nodiscard]] auto GetString() const
    {
        boost::container::small_vector<char, 16> decoded;
        if (uint64 token = m_data)
        {
            uint64 const tokenNum = token >> 60;
            for (token &= 0xFFFFFFFFFFFFFFF; token; token >>= 5)
                decoded.emplace_back(token & 0x1F ? '`' + (token & 0x1F) : ' ');
            if (tokenNum)
            {
                decoded.emplace_back('0' + tokenNum / 10);
                decoded.emplace_back('0' + tokenNum % 10);
            }
        }
        decoded.emplace_back('\0');
        return decoded;
    }
    [[nodiscard]] static uint64 FromString(std::string_view string)
    {
        uint64 token = 0;
        int len = string.size();

        uint64 tokenNum = 0;
        if (len >= 2 && isdigit(string[len - 2]) && isdigit(string[len - 1]))
        {
            tokenNum = (string[len - 2] - '0') * 10 + (string[len - 1] - '0');
            len -= 2;
        }

        for (int i = len - 1; i >= 0; --i)
        {
            char const c = string[i];
            token <<= 5;
            token |= c == ' ' ? 0 : c - '`';
        }

        token |= tokenNum << 60;

        return token;
    }
};

#pragma pack(push, 8)
struct Token
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
static Token& operator+=(Token& a, Token b)
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
};
#pragma pack(pop)

namespace detail
{
    consteval uint32 makefcc(char const (&magic)[4])
    {
        return magic[0] << 0 | magic[1] << 8 | magic[2] << 16;
    }
    consteval uint32 makefcc(char const (&magic)[5])
    {
        return magic[0] << 0 | magic[1] << 8 | magic[2] << 16 | magic[3] << 24;
    }
};

#define FourCC(magic) magic = detail::makefcc(#magic)
enum class fcc : uint32
{
    Empty = 0,

    PF1 = 0x00014650,
    PF2 = 0x00024650,
    PF3 = 0x00034650,
    PF4 = 0x00044650,
    PF5 = 0x00054650,

    FourCC(ATEX),
    FourCC(ATTX),
    FourCC(ATEC),
    FourCC(ATEP),
    FourCC(ATEU),
    FourCC(ATET),
    _3DCX = detail::makefcc("3DCX"),
    FourCC(DXT ),
    FourCC(DDS ),
    FourCC(strs),
    FourCC(asnd),
    FourCC(RIFF),
    TTF = 0x00000100,
    FourCC(OggS),
    FourCC(ARAP),
    FourCC(CTEX),

    // Texture codec
    FourCC(DXT1),
    FourCC(DXT2),
    FourCC(DXT3),
    FourCC(DXT4),
    FourCC(DXT5),
    FourCC(DXTN),
    FourCC(DXTL),
    FourCC(DXTA),
    FourCC(R32F),

    // RIFF FourCC
    FourCC(WEBP),

    // PF FourCC
    FourCC(ARMF),
    FourCC(ASND),
    FourCC(ABNK),
    FourCC(ABIX),
    FourCC(AMSP),
    FourCC(CDHS),
    FourCC(CINP),
    FourCC(cntc),
    FourCC(MODL),
    FourCC(GEOM),
    FourCC(DEPS),
    FourCC(EULA),
    FourCC(hvkC),
    FourCC(locl),
    FourCC(mapc),
    FourCC(mpsd),
    FourCC(PIMG),
    FourCC(PGTB),
    FourCC(AMAT),
    FourCC(anic),
    FourCC(emoc),
    FourCC(prlt),
    FourCC(cmpc),
    FourCC(txtm),
    FourCC(txtV),
    FourCC(txtv),
    PNG = 0x474e5089,
    FourCC(cmaC),
    FourCC(mMet),
    FourCC(AFNT),

    // PF Chunk FourCC
    FourCC(BKCK),
    FourCC(Main),
    FourCC(vari),
    FourCC(BIDX),
};
#undef FourCC
