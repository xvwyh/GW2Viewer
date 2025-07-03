export module GW2Viewer.Utils.Enum;
import GW2Viewer.Utils.ConstString;
import std;

namespace GW2Viewer
{

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

}

export namespace GW2Viewer
{

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

}

template<GW2Viewer::Enumeration T, typename CharT>
constexpr std::pair<T const, CharT const* const> EnumNames[];

template<GW2Viewer::Enumeration T, typename CharT>
struct std::formatter<T, CharT>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(T const& e, auto& ctx) const
    {
        static constexpr GW2Viewer::ConstString format = "{}";
        auto const name = ConstPairArrayLookup<EnumNames<T, /*TODO: CharT*/ char>>(e, {});
        if (!*name)
            return std::format_to(ctx.out(), format.get<CharT>(), std::to_underlying(e));
        return ranges::transform(std::string_view(name), ctx.out(), [](auto const c) -> wchar_t { return c; }).out;
    }
};
