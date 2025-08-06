module;
#include "Utils/Scan.h"

export module GW2Viewer.Common.GUID;
import GW2Viewer.Common;
import GW2Viewer.Common.JSON;
import GW2Viewer.Utils.ConstString;
import GW2Viewer.Utils.Scan;
import std;
#include "Macros.h"

export namespace GW2Viewer
{

#pragma pack(push, 1)
struct GUID
{
    static GUID const Empty;

    uint32 Data1 = 0;
    uint16 Data2 = 0;
    uint16 Data3 = 0;
    byte Data4[8] { 0, 0, 0, 0, 0, 0, 0, 0 };

    GUID() = default;
    GUID(std::span<byte const> data)
    {
        if (data.size() == sizeof(*this))
            memcpy(this, data.data(), sizeof(*this));
        else
            memset(this, 0, sizeof(*this));
    }
    GUID(std::span<uint32 const> data) : GUID(std::span((byte const*)data.data(), (byte const*)data.data() + data.size_bytes())) { }
    GUID(std::span<uint64 const> data) : GUID(std::span((byte const*)data.data(), (byte const*)data.data() + data.size_bytes())) { }
    GUID(std::vector<byte> const& data) : GUID(std::span(data)) { }
    GUID(std::string_view string);
    GUID(std::wstring_view string);
    GUID(char const* string) : GUID(std::string_view(string)) { }
    GUID(wchar_t const* string) : GUID(std::wstring_view(string)) { }

    constexpr auto operator<=>(GUID const&) const = default;
};
#pragma pack(pop)
static_assert(sizeof(GUID) == 16);

GUID const GUID::Empty { };

}

template<class CharT>
struct std::formatter<GW2Viewer::GUID, CharT>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(GW2Viewer::GUID const& guid, auto& ctx) const
    {
        return std::format_to(ctx.out(), "{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }
};

template<>
struct scn::scanner<GW2Viewer::GUID> : empty_parser
{
    template<typename Context>
    error scan(GW2Viewer::GUID& guid, Context& ctx)
    {
        std::basic_string<typename Context::char_type> str(std::from_range, ctx.range());
        int scanned = 0;
        if constexpr (std::is_same_v<typename Context::char_type, char>)
            scanned = sscanf_s(str.c_str(), "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX", &guid.Data1, &guid.Data2, &guid.Data3, &guid.Data4[0], &guid.Data4[1], &guid.Data4[2], &guid.Data4[3], &guid.Data4[4], &guid.Data4[5], &guid.Data4[6], &guid.Data4[7]);
        else if constexpr (std::is_same_v<typename Context::char_type, char>)
            scanned = wscanf_s(str.c_str(), L"%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX", &guid.Data1, &guid.Data2, &guid.Data3, &guid.Data4[0], &guid.Data4[1], &guid.Data4[2], &guid.Data4[3], &guid.Data4[4], &guid.Data4[5], &guid.Data4[6], &guid.Data4[7]);
        else
            static_assert("Char type not supported");

        static constexpr GW2Viewer::ConstString format = "{}";
        return scanned == 11 ? scan_usertype(ctx, format.get<typename Context::char_type>(), str) : error(error::invalid_scanned_value, "Mismatch with GUID format");
    }
};

GW2Viewer::GUID::GUID(std::string_view string) { Utils::Scan::Into(string, *this); }
GW2Viewer::GUID::GUID(std::wstring_view string) { Utils::Scan::Into(string, *this); }

template<>
struct std::hash<GW2Viewer::GUID>
{
    constexpr std::size_t operator()(GW2Viewer::GUID const& guid) const noexcept
    {
        std::size_t res = 17;
        for (auto part : std::bit_cast<std::array<size_t, 2>, GW2Viewer::GUID>(guid))
            res = res * 31 + std::hash<size_t>()(part);
        return res;
    }
};

SERIALIZE_AS_STRING(GW2Viewer::GUID)
