export module GW2Viewer.Utils;
import std;

export
{

template<typename T>
struct std::less<std::span<T>>
{
    bool operator()(std::span<T> const& a, std::span<T> const& b) const noexcept
    {
        return std::ranges::lexicographical_compare(a | std::views::take(24) | std::views::reverse, b | std::views::take(24) | std::views::reverse);
        //return std::ranges::lexicographical_compare(a | std::views::reverse, b | std::views::reverse);
    }
};

}

namespace proj
{
template <typename T> struct _addressof { T* operator()(T&& input) const { return &input; } };
template <typename T> struct _dereference { T& operator()(T&& input) const { return *input; } };

export template <typename T> constexpr _addressof<T> addressof;
export template <typename T> constexpr _dereference<T> dereference;
}
