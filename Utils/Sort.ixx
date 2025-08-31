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

template<typename T>
struct Unsorted
{
    Unsorted(T value, bool invert) : m_value(std::move(value)), m_invert(invert) { }
    Unsorted(Unsorted&& source) noexcept : m_value(std::move(source.m_value)), m_invert(source.m_invert) { }
    Unsorted& operator=(Unsorted&& source) noexcept { m_value = std::move(source.m_value); m_invert = source.m_invert; return *this; }

    auto operator==(Unsorted const& other) const { return m_value == other.m_value; } // Workaround for operator <=> not generating operators == and !=, without this std::ranges::sort will fall back to using 3 comparisons (< then > then ==) which is a performance loss
    auto operator<=>(Unsorted const& other) const
    {
        auto const order = m_value <=> other.m_value;
        return m_invert ? 0 <=> order : order;
    }

private:
    T m_value;
    bool m_invert;
};

template<typename Func>
struct Lazy
{
    Lazy(Func&& func) : m_func(std::move(func)) { }
    Lazy(Lazy&& source) noexcept : m_func(std::move(source.m_func)), m_value(std::move(source.m_value)) { }
    Lazy& operator=(Lazy&& source) noexcept { m_func.~Func(); new(&m_func) Func(std::move(source.m_func)); m_value = std::move(source.m_value); return *this; } // Workaround for lambdas not being copy- or move-assignable for some reason

    auto operator==(Lazy const& other) const { return Get() == other.Get(); } // Workaround for operator <=> not generating operators == and !=, without this std::ranges::sort will fall back to using 3 comparisons (< then > then ==) which is a performance loss
    auto operator<=>(Lazy const& other) const { return Get() <=> other.Get(); }

private:
    Func m_func;
    mutable std::optional<std::invoke_result_t<Func>> m_value;

    auto const& Get() const
    {
        if (!m_value)
            m_value.emplace(m_func());
        return *m_value;
    }
};

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
