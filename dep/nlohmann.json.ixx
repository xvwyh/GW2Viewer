
// Global module fragment where #includes can happen
module;

// included headers will not be exported to the client
#include <nlohmann/json.hpp>

// first thing after the Global module fragment must be a module command
export module nlohmann.json;

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
    using ::nlohmann::to_json;
    using ::nlohmann::from_json;

    using ::nlohmann::adl_serializer;
  }
}

module : private;
// possible to include headers here, but they will not be exported to the client