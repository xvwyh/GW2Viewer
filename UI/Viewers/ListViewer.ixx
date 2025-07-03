export module GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.Common;
import GW2Viewer.UI.Viewers.Viewer;
import GW2Viewer.Utils.Scan;
import std;

template<typename Index, typename ComplexIndex>
auto defaultComplexSortComparison(Index const a, Index const b, ComplexIndex const& aTransformed, ComplexIndex const& bTransformed) -> bool
{
    #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
    COMPARE(aTransformed, bTransformed);
    COMPARE(a, b);
    return false;
    #undef COMPARE
}

export namespace GW2Viewer::UI::Viewers
{

struct ListViewerBase : Viewer
{
    using Viewer::Viewer;

    struct CompareResult
    {
        CompareResult(std::strong_ordering result) : m_result(result) { }
        operator bool() const { return m_result != std::strong_ordering::equal; }
        operator std::strong_ordering() const { return m_result; }
    private:
        std::strong_ordering m_result;
    };
    static constexpr CompareResult Compare(auto&& a, auto&& b) { return a <=> b; }

    template<typename Range, typename Index = typename Range::value_type, typename Transform, typename ComplexIndex = decltype(Transform{}(Index{})), typename Comparison = bool(Index, Index, ComplexIndex const&, ComplexIndex const&)>
    static constexpr void ComplexSort(Range& data, bool invert, Transform const& transform, Comparison const& function = defaultComplexSortComparison)
    {
        std::vector sortable { std::from_range, data | std::views::transform([&transform](auto const& id) { return std::pair { id, transform(id) }; }) };
        std::ranges::sort(sortable, [invert, &function](auto const& a, auto const& b) -> bool { return function(a.first, b.first, a.second, b.second) ^ invert; });
        data.assign_range(sortable | std::views::keys);
    }
};

}

export namespace GW2Viewer::G::Viewers { template<typename T> std::list<T*> ListViewers; }

export namespace GW2Viewer::UI::Viewers
{

template<typename T>
struct ListViewer : ListViewerBase
{
    ListViewer(uint32 id, bool newTab) : ListViewerBase(id, newTab)
    {
        G::Viewers::ListViewers<T>.emplace_back((T*)this);
    }
    ~ListViewer() override
    {
        G::Viewers::ListViewers<T>.remove((T*)this);
    }
};

}
