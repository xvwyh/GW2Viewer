export module GW2Viewer.Utils.ConstString;
import std;

export
{

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

}
