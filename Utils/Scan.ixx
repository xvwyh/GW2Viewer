module;
#include "Utils/Scan.h"

export module GW2Viewer.Utils.Scan;
import GW2Viewer.Utils.ConstString;
import <algorithm>;
import <variant>;

namespace GW2Viewer
{

template<typename Result>
struct ResultBase
{
    Result WrappedResult;

    ResultBase(Result&& result) : WrappedResult(std::move(result)) { }

    auto Rest() const { return WrappedResult.subrange(); }

    bool PartialMatch() const { return (bool)WrappedResult; }
    bool FullMatch() const { return PartialMatch() && WrappedResult.empty(); }
    bool FullMatchIgnoringWhitespace() const { return FullMatch() || PartialMatch() && std::ranges::all_of(WrappedResult.subrange(), isspace); }
};

}

export namespace GW2Viewer::Utils::Scan
{

template<typename Range>
struct Result : ResultBase<scn::detail::scan_result_for_range<Range>>
{
    using ResultBase<scn::detail::scan_result_for_range<Range>>::ResultBase;

    operator bool() const { return this->FullMatchIgnoringWhitespace(); }
};
template<typename Range, typename T>
struct ResultValue : ResultBase<scn::detail::generic_scan_result_for_range<scn::expected<T>, Range>>
{
    using ResultBase<scn::detail::generic_scan_result_for_range<scn::expected<T>, Range>>::ResultBase;

    auto HasValue() const { return this->WrappedResult.has_value(); }
    auto& Value() & { return this->WrappedResult.value(); }
    auto Value() const& { return this->WrappedResult.value(); }
    auto Value() && { return std::move(this->WrappedResult.value()); }
    auto operator*() const { return Value(); }

    operator bool() const { return HasValue(); }
};
template<typename Range>
struct ResultNumberLiteral : Result<Range>
{
    using Result<Range>::Result;

    operator bool() const
    {
        if (this->FullMatchIgnoringWhitespace())
            return true;
        if (!this->PartialMatch())
            return false;
        if (auto rest = std::string_view(this->Rest());
            rest == "u" || rest == "U" ||
            rest == "ul" || rest == "UL" ||
            rest == "ull" || rest == "ULL" ||
            rest == "l" || rest == "L" ||
            rest == "ll" || rest == "LL" ||
            rest == "i64" || rest == "I64" ||
            rest == "ui64" || rest == "UI64" ||
            rest == "h")
            return true;
        return false;
    }
};

template<typename Range, typename Format, typename... Args>
[[nodiscard]] auto Into(Range&& range, Format const& f, Args&... a)
{
    return Result<Range> { scn::scan(std::forward<Range>(range), f, a...) };
}

template<typename Range, typename Args>
[[nodiscard]] auto Into(Range&& range, Args& a)
{
    static constexpr ConstString format = "{}";
    return Result<Range> { scn::scan(std::forward<Range>(range), format.get<typename scn::detail::extract_char_type<scn::ranges::iterator_t<scn::range_wrapper_for_t<Range>>>::type>(), a) };
}

template<typename T, typename Range>
auto Single(Range&& range)
{
    return ResultValue<Range, T> { scn::scan_value<T>(std::forward<Range>(range)) };
}

template<typename T, typename Range>
T Single(Range&& range, T def)
{
    auto result = scn::scan_value<T>(std::forward<Range>(range));
    return result.has_value() ? result.value() : def;
}

template<typename T, typename Range, typename Format>
auto NumberLiteral(Range&& range, Format const& f, T& target)
{
    return ResultNumberLiteral<Range> { scn::scan(std::forward<Range>(range), f, target) };
}

template<typename T, typename Range>
auto NumberLiteral(Range&& range, T& target)
{
    return NumberLiteral(std::forward<Range>(range), "{}", target);
}

}
