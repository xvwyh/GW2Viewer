module;
#include <time.h>

export module GW2Viewer.Utils.Format;
import GW2Viewer.Common;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Common.Time;
import GW2Viewer.Utils.ConstString;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Visitor;
import std;
import <format>;

export namespace std
{

template<typename T, typename CharT>
struct formatter<optional<T>, CharT>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(optional<T> const& opt, auto& ctx) const
    {
        static constexpr GW2Viewer::ConstString empty = "<empty>";
        static constexpr GW2Viewer::ConstString format = "{}";
        if (!opt.has_value())
            return format_to(ctx.out(), format.get<CharT>(), empty.get<CharT>());
        return format_to(ctx.out(), format.get<CharT>(), *opt);
    }
};

/*
template<>
struct formatter<char const*, wchar_t>
{
    constexpr auto parse(wformat_parse_context& ctx) { return ctx.begin(); }
    auto format(char const* const& str, wformat_context& ctx) const
    {
        return ranges::transform(string_view(str), ctx.out(), [](char const c) -> wchar_t { return c; }).out;
    }
};
*/
template<>
struct formatter<u8string, wchar_t>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(u8string const& str, auto& ctx) const
    {
        return format_to(ctx.out(), L"{}", GW2Viewer::Utils::Encoding::FromUTF8(str));
    }
};


template<>
struct formatter<wstring_view, char>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(wstring_view const& wstring, auto& ctx) const
    {
        return format_to(ctx.out(), "{}", GW2Viewer::Utils::Encoding::ToUTF8(wstring));
    }
};

template<>
struct formatter<wstring, char>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(wstring const& wstring, auto& ctx) const
    {
        return format_to(ctx.out(), "{}", GW2Viewer::Utils::Encoding::ToUTF8(wstring));
    }
};

template <class _Context = format_context, class... _Args>
[[nodiscard]] auto make_format_args(_Args const&... _Vals)
{
    if constexpr ((_Formattable_with_non_const<remove_const_t<_Args>, _Context> && ...))
    {
        static_assert((_Formattable_with<remove_const_t<_Args>, _Context> && ...),
            "The format() member function can't be called on const formatter<T>. "
            "To make the formatter usable, add const to format(). "
            "See N4971 [format.arg.store]/2 and [formatter.requirements].");
    }
    else
    {
        static_assert((_Formattable_with<remove_const_t<_Args>, _Context> && ...),
            "Cannot format an argument. To make T formattable, provide a formatter<T> specialization. "
            "See N4971 [format.arg.store]/2 and [formatter.requirements].");
    }
    return _Format_arg_store<_Context, _Args...>{forward<_Args&>(const_cast<_Args&>(_Vals))...};
}

template <class... _Args, typename = enable_if_t<!conjunction_v<is_const<_Args>>>>
[[nodiscard]] auto make_wformat_args(_Args const&... _Vals)
{
    return make_format_args<wformat_context>(_Vals...);
}

}

export namespace GW2Viewer::Utils::Format
{

template<typename Rep, typename Period>
std::string DurationShort(std::chrono::duration<Rep, Period> duration)
{
    using namespace std::chrono;

    constexpr std::tuple units { years(1), months(1), days(1), 1h, 1min, 1s, 1ms, 1us, 1ns };

    std::string result;

    auto suffix = Visitor::Overloaded
    {
        [](years const&) { return "y"; },
        [](months const&) { return "mo"; },
        [](minutes const&) { return "m"; },
        [](auto const& duration) { return std::format("{:%q}", duration); }
    };
    auto append = [&]<typename T>(T const& unit)
    {
        if constexpr (std::ratio_less_equal_v<Period, typename T::period>)
        {
            if (auto const val = duration_cast<T>(duration); val.count() > 0)
            {
                result += std::format("{}{} ", val.count(), suffix(unit));
                duration -= val;
            }
        }
    };

    std::apply([&](auto const&... unit) { (append(unit), ...); }, units);

    if (!result.empty())
        result.pop_back();
    return result;
}

template<typename Rep, typename Period>
std::string DurationShortColored(char const* format, std::chrono::duration<Rep, Period> duration)
{
    auto const color =
        duration < 10s ? "F00" :
        duration < 1min ? "F40" :
        duration < 10min ? "F80" :
        duration < 30min ? "FB0" :
        duration < 1h ? "FD0" :
        duration < 3h ? "FF0" :
        duration < 5h ? "FF8" :
        duration < 10h ? "FFC" :
        duration < 24h ? "FFF" :
        duration < 48h ? "C" :
        duration < 72h ? "8" : "4";
    return std::format("<c=#{}>{}</c>", color, std::vformat(format, std::make_format_args(DurationShort(duration))));
}

std::string DateTimeFull(Time::Point time)
{
    try { return std::format("{:%F %T}", Time::ToSecs(time)); }
    catch (...)
    {
        auto const timestamp = Time::ToTimestamp(time);
        tm tm { };
        gmtime_s(&tm, &timestamp);
        return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
}

std::string DateTimeFullLocal(Time::Point time)
{
    try { return std::format("{:%F %T}", std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(time))); }
    catch (...)
    {
        auto const timestamp = Time::ToTimestamp(time);
        tm tm { };
        localtime_s(&tm, &timestamp);
        return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
}

union PrintableFourCC
{
    uint64 Number;
    char Chars[5];

    PrintableFourCC(uint32 fourCC) : Number(fourCC)
    {
        for (auto& c : Chars)
            if (!c || !isprint(c) && !isspace(c))
                c = '?';
        Chars[4] = '\0';
        for (auto& c : Chars | std::views::reverse)
        {
            if (!c || c == '?')
                c = '\0';
            else
                break;
        }
    }
    PrintableFourCC(fcc fourCC) : PrintableFourCC((uint32)fourCC) { }
};

}

export namespace std
{

template<>
struct formatter<GW2Viewer::Utils::Format::PrintableFourCC, char>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(GW2Viewer::Utils::Format::PrintableFourCC const& value, auto& ctx) const
    {
        return format_to(ctx.out(), "{}", value.Chars);
    }
};

}
