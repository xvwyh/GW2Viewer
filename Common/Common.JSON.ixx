module;
//#include <nlohmann/json.hpp>
#include "Utils/Utils.Scan.h"

export module GW2Viewer.Common.JSON;
//export import nlohmann.json;
import GW2Viewer.Utils.ConstString;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Format;
import std;
import <nlohmann/json.hpp>;

// build up namespace from macros
#define NLOHMANN_NS NLOHMANN_JSON_NAMESPACE_CONCAT( \
  NLOHMANN_JSON_ABI_TAGS, \
  NLOHMANN_JSON_NAMESPACE_VERSION)

export
{
  namespace nlohmann::NLOHMANN_NS::detail
  {
    using ::nlohmann::NLOHMANN_NS::detail::json_sax_dom_callback_parser;
    using ::nlohmann::NLOHMANN_NS::detail::json_sax_dom_parser;
    using ::nlohmann::NLOHMANN_NS::detail::iteration_proxy_value;
    using ::nlohmann::NLOHMANN_NS::detail::iteration_proxy;
    using ::nlohmann::NLOHMANN_NS::detail::iter_impl;
    using ::nlohmann::NLOHMANN_NS::detail::unknown_size;
    using ::nlohmann::NLOHMANN_NS::detail::enable_if_t;
    using ::nlohmann::NLOHMANN_NS::detail::is_basic_json;
  }

  namespace nlohmann::NLOHMANN_NS::literals
  {
    using ::nlohmann::NLOHMANN_NS::literals::operator""_json;
    using ::nlohmann::NLOHMANN_NS::literals::operator""_json_pointer;
  }



  namespace nlohmann
  {
    using ::nlohmann::basic_json;
    using ::nlohmann::json;
    using ::nlohmann::json_sax;
    using ::nlohmann::ordered_json;
    using ::nlohmann::detail::json_sax_dom_parser;
    using ::nlohmann::detail::json_sax_dom_callback_parser;
    using ::nlohmann::detail::iteration_proxy_value;
    using ::nlohmann::detail::iteration_proxy;
    using ::nlohmann::detail::iter_impl;
    using ::nlohmann::detail::unknown_size;
    using ::nlohmann::detail::enable_if_t;
    using ::nlohmann::detail::is_basic_json;
    using ::nlohmann::to_json;
    using ::nlohmann::from_json;

    using ::nlohmann::adl_serializer;
  }
}

export using nlohmann::json;
export using nlohmann::ordered_json;

export
{

template<>
struct nlohmann::adl_serializer<std::wstring>
{
    static void to_json(auto& j, std::wstring const& str) { j = Utils::Encoding::ToUTF8(str); }
    static void from_json(auto const& j, std::wstring& str) { str = Utils::Encoding::FromUTF8(j.template get<std::string>()); }
};

template<>
struct nlohmann::adl_serializer<std::u8string>
{
    static void to_json(auto& j, std::u8string const& str) { j = { str.begin(), str.end() }; }
    static void from_json(auto const& j, std::u8string& str) { auto utf8 = j.template get<std::string>(); str = { utf8.begin(), utf8.end() }; }
};

template<>
struct scn::scanner<std::wstring> : empty_parser
{
    template<typename Context>
    error scan(std::wstring& wstring, Context& ctx)
    {
        static constexpr ConstString format = "{}";
        std::string utf8;
        auto result = scan_usertype(ctx, format.get<typename Context::char_type>(), utf8);
        wstring = Utils::Encoding::FromUTF8(utf8);
        return result;
    }
};

template<typename T>
struct nlohmann::adl_serializer<std::optional<T>>
{
    static void to_json(auto& j, std::optional<T> const& opt)
    {
        if (opt)
            j = *opt;
        else
            j = nullptr;
    }
    static void from_json(auto const& j, std::optional<T>& opt)
    {
        if (j.is_null())
            opt.reset();
        else
            opt = j.template get<T>();
    }
}; 

template<typename T>
concept StringConvertibleKey =
       requires(T key) { { std::format("{}", key) } -> std::convertible_to<std::string>; }
    && requires(T key) { { std::format(L"{}", key) } -> std::convertible_to<std::wstring>; }
    && requires { { scn::scan_value<T>("").value() } -> std::convertible_to<T>; }
    && requires { { scn::scan_value<T>(L"").value() } -> std::convertible_to<T>; }
;

template<StringConvertibleKey T>
struct StringConvertibleSerializer
{
    static void to_json(auto& j, T const& value) { j = std::format("{}", value); }
    static void from_json(auto const& j, T& value) { value = scn::scan_value<T>(j.template get<std::string_view>()).value(); }
};


template<>
struct std::formatter<std::wstring, char>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(std::wstring const& wstring, auto& ctx) const
    {
        return format_to(ctx.out(), "{}", Utils::Encoding::ToUTF8(wstring));
    }
};

template<StringConvertibleKey K, typename V>
struct nlohmann::adl_serializer<std::map<K, V>>
{
    static void to_json(auto& j, std::map<K, V> const& map)
    {
        for (auto const& [k, v] : map)
            j.emplace(std::format("{}", k), v);
    }
    static void from_json(auto const& j, std::map<K, V>& map)
    {
        for (auto const& [k, v] : j.items())
            map.emplace(scn::scan_value<K>(k).value(), v.template get<V>());
    }
};

template<typename Clock, typename Duration>
struct nlohmann::adl_serializer<std::chrono::time_point<Clock, Duration>>
{
    static void to_json(auto& j, std::chrono::time_point<Clock, Duration> const& time)
    {
        j = std::format("{:%FT%T%Ez}", time);
    }
    static void from_json(auto const& j, std::chrono::time_point<Clock, Duration>& time)
    {
        std::istringstream(j.template get<std::string>()) >> std::chrono::parse("%FT%T%Ez", time);
    }
};

template<typename Rep, typename Period>
struct nlohmann::adl_serializer<std::chrono::duration<Rep, Period>>
{
    static void to_json(auto& j, std::chrono::duration<Rep, Period> const& duration)
    {
        auto const ticks = duration.count();
             if (!(ticks % (std::chrono::days   ::period::num * std::chrono::milliseconds::period::den))) j = std::format("{:%Q%q}", std::chrono::duration_cast<std::chrono::days        >(duration));
        else if (!(ticks % (std::chrono::hours  ::period::num * std::chrono::milliseconds::period::den))) j = std::format("{:%Q%q}", std::chrono::duration_cast<std::chrono::hours       >(duration));
        else if (!(ticks % (std::chrono::minutes::period::num * std::chrono::milliseconds::period::den))) j = std::format("{:%Q%q}", std::chrono::duration_cast<std::chrono::minutes     >(duration));
        else if (!(ticks % (std::chrono::seconds::period::num * std::chrono::milliseconds::period::den))) j = std::format("{:%Q%q}", std::chrono::duration_cast<std::chrono::seconds     >(duration));
        else                                                                                              j = std::format("{:%Q%q}", std::chrono::duration_cast<std::chrono::milliseconds>(duration));
    }
    static void from_json(auto const& j, std::chrono::duration<Rep, Period>& duration)
    {
        if (j.is_number())
        {
            duration = { j.template get<Rep>() };
            return;
        }

        std::string str;
        j.get_to(str);
        Rep ticks;
        if (!scn::scan(str, ticks))
            return;
        if (str.ends_with("ms"))
            duration = std::chrono::milliseconds(ticks);
        else if (str.ends_with("s"))
            duration = std::chrono::seconds(ticks);
        else if (str.ends_with("min"))
            duration = std::chrono::minutes(ticks);
        else if (str.ends_with("h"))
            duration = std::chrono::hours(ticks);
        else if (str.ends_with("d"))
            duration = std::chrono::days(ticks);
        else
            duration = std::chrono::milliseconds(ticks);
    }
};

}
