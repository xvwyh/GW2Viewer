#pragma once
#include "JSON.h"
#include "Utils.h"
#include <array>
#include <bit>
#include <format>
#include <scn/reader/common.h>

#define GUID_DEFINED
#define _SYS_GUID_OPERATOR_EQ_
#pragma pack(push, 1)
struct GUID
{
    uint32_t Data1 = 0;
    uint16_t Data2 = 0;
    uint16_t Data3 = 0;
    uint8_t Data4[8] { 0, 0, 0, 0, 0, 0, 0, 0 };

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

    constexpr auto operator<=>(GUID const&) const = default;
};
#pragma pack(pop)

static_assert(sizeof(GUID) == 16);
static constexpr GUID EmptyGUID { };

template<class CharT>
struct std::formatter<GUID, CharT>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(GUID const& guid, auto& ctx) const
    {
        return std::format_to(ctx.out(), "{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }
};

template<>
struct scn::scanner<GUID> : empty_parser
{
    template<typename Context>
    error scan(GUID& guid, Context& ctx)
    {
        std::basic_string<typename Context::char_type> str(std::from_range, ctx.range());
        int scanned = 0;
        if constexpr (std::is_same_v<typename Context::char_type, char>)
            scanned = sscanf_s(str.c_str(), "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX", &guid.Data1, &guid.Data2, &guid.Data3, &guid.Data4[0], &guid.Data4[1], &guid.Data4[2], &guid.Data4[3], &guid.Data4[4], &guid.Data4[5], &guid.Data4[6], &guid.Data4[7]);
        else if constexpr (std::is_same_v<typename Context::char_type, char>)
            scanned = wscanf_s(str.c_str(), L"%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX", &guid.Data1, &guid.Data2, &guid.Data3, &guid.Data4[0], &guid.Data4[1], &guid.Data4[2], &guid.Data4[3], &guid.Data4[4], &guid.Data4[5], &guid.Data4[6], &guid.Data4[7]);
        else
            static_assert("Char type not supported");

        static constexpr ConstString format = "{}";
        return scanned == 11 ? scan_usertype(ctx, format.get<typename Context::char_type>(), str) : error(error::invalid_scanned_value, "Mismatch with GUID format");
    }
};

template<>
struct std::hash<GUID>
{
    constexpr std::size_t operator()(GUID const& guid) const noexcept
    {
        std::size_t res = 17;
        for (auto part : std::bit_cast<std::array<size_t, 2>, GUID>(guid))
            res = res * 31 + std::hash<size_t>()(part);
        return res;
    }
};

SERIALIZE_AS_STRING(GUID)
