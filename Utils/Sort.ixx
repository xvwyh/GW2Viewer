export module GW2Viewer.Utils.Sort;
import std;

namespace GW2Viewer::Utils::Sort
{

template<typename Index, typename ComplexIndex>
auto defaultComplexSortComparison(Index const a, Index const b, ComplexIndex const& aTransformed, ComplexIndex const& bTransformed) -> bool
{
    #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
    COMPARE(aTransformed, bTransformed);
    if constexpr (std::three_way_comparable<Index const>)
        COMPARE(a, b);
    return false;
    #undef COMPARE
}

export
{

template<typename Range, typename Index = std::ranges::range_value_t<Range>, typename Transform, typename ComplexIndex = std::invoke_result_t<Transform, Index>, typename Comparison = bool(Index, Index, ComplexIndex const&, ComplexIndex const&)>
constexpr void ComplexSort(Range& data, bool invert, Transform&& transform, Comparison&& comparison = defaultComplexSortComparison)
{
    std::vector sortable { std::from_range, data | std::views::transform([transform = std::move(transform)](auto const& id) { return std::pair { id, transform(id) }; }) };
    std::ranges::sort(sortable, [invert, comparison = std::move(comparison)](auto const& a, auto const& b) -> bool { return comparison(a.first, b.first, a.second, b.second) ^ invert; });
    data.assign_range(sortable | std::views::keys);
}

template<typename Range, typename Index = std::ranges::range_value_t<Range>, typename Transform, typename ComplexIndex = std::invoke_result_t<Transform, Index>, typename Comparison = bool(Index, Index, ComplexIndex const&, ComplexIndex const&)>
constexpr std::vector<Index> ComplexSorted(Range const& data, bool invert, Transform&& transform, Comparison&& comparison = defaultComplexSortComparison)
{
    std::vector<Index> sorted { std::from_range, data };
    ComplexSort(sorted, invert, std::move(transform), std::move(comparison));
    return sorted;
}

}

}
