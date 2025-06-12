#pragma once
#include "Common.h"

#include <algorithm>
#include <format>
#include <utility>
#include <vector>
#include <scn/scn.h>

#define STATIC(variable) decltype(variable) variable

std::string to_utf8(std::wstring_view str);
std::wstring from_utf8(std::string_view str);
std::wstring from_utf8(std::u8string_view str);
std::wstring to_wstring(std::string_view str);

template<std::size_t N>
struct ConstString
{
    constexpr ConstString(char const(&s)[N])
    {
        std::ranges::copy(s, str);
        std::ranges::copy(s, wstr);
    }

    template<typename Ch> [[nodiscard]] constexpr std::basic_string_view<Ch> get() const;
    template<> [[nodiscard]] constexpr std::basic_string_view<char> get<char>() const { return str; }
    template<> [[nodiscard]] constexpr std::basic_string_view<wchar_t> get<wchar_t>() const { return wstr; }

    char str[N];
    wchar_t wstr[N];
};

template<typename T, typename CharT>
struct std::formatter<std::optional<T>, CharT>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(std::optional<T> const& opt, auto& ctx) const
    {
        static constexpr ConstString empty = "<empty>";
        static constexpr ConstString format = "{}";
        if (!opt.has_value())
            return std::format_to(ctx.out(), format.get<CharT>(), empty.get<CharT>());
        return std::format_to(ctx.out(), format.get<CharT>(), *opt);
    }
};

template <class F>
class final_action
{
public:
    explicit final_action(const F& ff) noexcept : f{ ff } { }
    explicit final_action(F&& ff) noexcept : f{ std::move(ff) } { }

    ~final_action() noexcept { if (invoke) f(); }

    final_action(final_action&& other) noexcept
        : f(std::move(other.f)), invoke(std::exchange(other.invoke, false))
    { }

    final_action(const final_action&) = delete;
    void operator=(const final_action&) = delete;
    void operator=(final_action&&) = delete;

private:
    F f;
    bool invoke = true;
};

// finally() - convenience function to generate a final_action
template <class F>
[[nodiscard]] auto finally(F&& f) noexcept
{
    return final_action<std::decay_t<F>>{std::forward<F>(f)};
}

class scoped_seh_exception_handler
{
    _se_translator_function const old;

public:
    static scoped_seh_exception_handler Create();

    scoped_seh_exception_handler(_se_translator_function handler) noexcept;
    ~scoped_seh_exception_handler() noexcept;
};

template<class... Ts>
struct pack : Ts...
{
    pack() : Ts()... { }
    pack(Ts const&... sources) : Ts(sources)... { }
    pack(Ts&&... sources) : Ts(std::move(sources))... { }

    std::strong_ordering operator<=>(pack const&) const = default;
};

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<const auto& PairArray,
    typename K = decltype(std::begin(PairArray)->first),
    typename V = decltype(std::begin(PairArray)->second)>
constexpr V ConstPairArrayLookup(K const key, V const defaultValue = V())
{
    static constexpr auto N = std::size(PairArray);
    if ((size_t)key >= N)
        return defaultValue;
    static constexpr auto table = []<size_t... I>(std::integer_sequence<size_t, I...>) consteval
    {
        constexpr std::array<V const, N> table { std::find_if(std::begin(PairArray), std::end(PairArray), [](auto pair) { return pair.first == (K)I; })->second... };
        return table;
    }(std::make_integer_sequence<size_t, N>{});
    return table[(size_t)key];
}

template<typename T>
concept Enumeration = std::is_enum_v<T>;

template<typename T>
concept ScopedEnumeration = Enumeration<T> && std::is_scoped_enum_v<T>;

template<ScopedEnumeration E> struct EnumFlagsWrapper
{
    E value;
    constexpr EnumFlagsWrapper() noexcept = default;
    constexpr EnumFlagsWrapper(E v) noexcept : value(v) { }
    constexpr EnumFlagsWrapper(std::underlying_type_t<E> v) noexcept : value((E)v) { }
    constexpr operator E() const noexcept { return value; }
    constexpr operator bool() const noexcept { return (bool)value; }
};
template<ScopedEnumeration E> constexpr E operator~(E rhs) noexcept { return E(~std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator~(EnumFlagsWrapper<E> rhs) noexcept { return E(~std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator|(E lhs, E rhs) noexcept { return E(std::to_underlying((E)lhs) | std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator|(EnumFlagsWrapper<E> lhs, E rhs) noexcept { return E(std::to_underlying((E)lhs) | std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator|(E lhs, EnumFlagsWrapper<E> rhs) noexcept { return E(std::to_underlying((E)lhs) | std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator|(EnumFlagsWrapper<E> lhs, EnumFlagsWrapper<E> rhs) noexcept { return E(std::to_underlying((E)lhs) | std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr EnumFlagsWrapper<E> operator&(E lhs, E rhs) noexcept { return EnumFlagsWrapper<E>(std::to_underlying((E)lhs) & std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr EnumFlagsWrapper<E> operator&(EnumFlagsWrapper<E> lhs, E rhs) noexcept { return EnumFlagsWrapper<E>(std::to_underlying((E)lhs) & std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr EnumFlagsWrapper<E> operator&(E lhs, EnumFlagsWrapper<E> rhs) noexcept { return EnumFlagsWrapper<E>(std::to_underlying((E)lhs) & std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr EnumFlagsWrapper<E> operator&(EnumFlagsWrapper<E> lhs, EnumFlagsWrapper<E> rhs) noexcept { return EnumFlagsWrapper<E>(std::to_underlying((E)lhs) & std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator^(E lhs, E rhs) noexcept { return E(std::to_underlying((E)lhs) ^ std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator^(EnumFlagsWrapper<E> lhs, E rhs) noexcept { return E(std::to_underlying((E)lhs) ^ std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator^(E lhs, EnumFlagsWrapper<E> rhs) noexcept { return E(std::to_underlying((E)lhs) ^ std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E operator^(EnumFlagsWrapper<E> lhs, EnumFlagsWrapper<E> rhs) noexcept { return E(std::to_underlying((E)lhs) ^ std::to_underlying((E)rhs)); }
template<ScopedEnumeration E> constexpr E& operator|=(E& lhs, E rhs) noexcept { return lhs = lhs | rhs; }
template<ScopedEnumeration E> constexpr E& operator&=(E& lhs, E rhs) noexcept { return lhs = lhs & rhs; }
template<ScopedEnumeration E> constexpr E& operator^=(E& lhs, E rhs) noexcept { return lhs = lhs ^ rhs; }

template<Enumeration T, typename CharT>
static constexpr std::pair<T const, CharT const* const> EnumNames[];

#define DECLARE_ENUM_NAMES(type) template<> static constexpr std::pair<type const, char const* const> EnumNames<type, char>[]

template<Enumeration T, typename CharT>
struct std::formatter<T, CharT>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(T const& e, auto& ctx) const
    {
        static constexpr ConstString format = "{}";
        auto const name = ConstPairArrayLookup<EnumNames<T, /*TODO: CharT*/ char>>(e, {});
        if (!*name)
            return std::format_to(ctx.out(), format.get<CharT>(), std::to_underlying(e));
        return ranges::transform(std::string_view(name), ctx.out(), [](auto const c) -> wchar_t { return c; }).out;
    }
};
/*
template<>
struct std::formatter<char const*, wchar_t>
{
    constexpr auto parse(std::wformat_parse_context& ctx) { return ctx.begin(); }
    auto format(char const* const& str, std::wformat_context& ctx) const
    {
        return ranges::transform(std::string_view(str), ctx.out(), [](char const c) -> wchar_t { return c; }).out;
    }
};
*/
template<>
struct std::formatter<std::u8string, wchar_t>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(std::u8string const& str, auto& ctx) const
    {
        return std::format_to(ctx.out(), L"{}", from_utf8(str));
    }
};

inline void replace_all(std::string& str, std::string_view from, std::string_view to)
{
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

inline void replace_all(std::wstring& str, std::wstring_view from, std::wstring_view to)
{
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::wstring::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

inline std::vector<std::string_view> split(std::string_view s, std::string_view delimiter)
{
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string_view> res;

    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos)
    {
        token = s.substr(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back(token);
    }

    res.push_back(s.substr(pos_start));
    return res;
}

template<typename Container, typename T>
auto FindInContainer(Container&& container, T&& value)
{
    auto const itr = container.find(value);
    return itr != container.end() ? &*itr : nullptr;
}

template<typename Map, typename T>
auto FindInMap(Map&& map, T&& value)
{
    auto const itr = map.find(value);
    return itr != map.end() ? &itr->second : nullptr;
}

template<typename Set, typename T>
bool SetPresenceToggle(Set& set, T const& element, bool present)
{
    if (present)
        set.emplace(element);
    else
        set.erase(element);
    return present;
}

template<typename T>
auto Remap(T const& in, T const& inMin, T const& inMax, T const& outMin, T const& outMax)
{
    return outMin + (outMax - outMin) * (in - inMin) / (inMax - inMin);
}

template<typename T>
auto ExpDecay(T const& current, T const& target, float decay, float deltaTime)
{
    return target + (current - target) * std::exp(-decay * deltaTime);
}

template<typename T>
struct std::less<std::span<T>>
{
    bool operator()(std::span<T> const& a, std::span<T> const& b) const noexcept
    {
        return std::ranges::lexicographical_compare(a | std::views::take(24) | std::views::reverse, b | std::views::take(24) | std::views::reverse);
        //return std::ranges::lexicographical_compare(a | std::views::reverse, b | std::views::reverse);
    }
};

namespace std
{
    template <class _Context = std::format_context, class... _Args>
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
        return _Format_arg_store<_Context, _Args...>{std::forward<_Args&>(const_cast<_Args&>(_Vals))...};
    }

    template <class... _Args, typename = std::enable_if_t<!std::conjunction_v<std::is_const<_Args>>>>
    [[nodiscard]] auto make_wformat_args(_Args const&... _Vals)
    {
        return make_format_args<wformat_context>(_Vals...);
    }
}

namespace proj
{
    struct { auto* operator()(auto&& input) const { return &input; } } addressof;
    struct { auto& operator()(auto&& input) const { return *input; } } dereference;
}
