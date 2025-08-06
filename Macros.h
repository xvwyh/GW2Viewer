#pragma once

#define STATIC(variable) decltype(variable) variable

#pragma region GW2Viewer.Common.JSON
import <nlohmann/detail/macro_scope.hpp>;

/*
#define NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT(Type, ...)  \
    template<typename BasicJsonType, nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0> \
    friend void to_json(BasicJsonType& nlohmann_json_j, const Type& nlohmann_json_t) { NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__)) } \
    template<typename BasicJsonType, nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0> \
    friend void from_json(const BasicJsonType& nlohmann_json_j, Type& nlohmann_json_t) { const Type nlohmann_json_default_obj{}; NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, __VA_ARGS__)) }

#define NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT_OMITTED(Type, ...)  \
    template<typename BasicJsonType, nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0> \
    friend void to_json(BasicJsonType& nlohmann_json_j, const Type& nlohmann_json_t) { const Type nlohmann_json_default_obj{}; if (nlohmann_json_j.is_null()) { nlohmann_json_j = BasicJsonType::object(); } NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO_WITH_DEFAULT, __VA_ARGS__)) } \
    template<typename BasicJsonType, nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0> \
    friend void from_json(const BasicJsonType& nlohmann_json_j, Type& nlohmann_json_t) { const Type nlohmann_json_default_obj{}; NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, __VA_ARGS__)) }
*/

#define NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT(Type, ...)  \
    friend void to_json(ordered_json& nlohmann_json_j, const Type& nlohmann_json_t) { NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__)) } \
    friend void from_json(const ordered_json& nlohmann_json_j, Type& nlohmann_json_t) { const Type nlohmann_json_default_obj{}; NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, __VA_ARGS__)) }

#define NLOHMANN_JSON_TO_WITH_DEFAULT(v1) \
    if constexpr (!std::equality_comparable<std::decay_t<decltype(nlohmann_json_t.v1)>>) \
    { nlohmann_json_j[#v1] = nlohmann_json_t.v1; } \
    else \
    { if (nlohmann_json_t.v1 != nlohmann_json_default_obj.v1) { nlohmann_json_j[#v1] = nlohmann_json_t.v1; } }


#define NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT_OMITTED(Type, ...)  \
    friend void to_json(ordered_json& nlohmann_json_j, const Type& nlohmann_json_t) { const Type nlohmann_json_default_obj{}; if (nlohmann_json_j.is_null()) { nlohmann_json_j = ordered_json::object(); } NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO_WITH_DEFAULT, __VA_ARGS__)) } \
    friend void from_json(const ordered_json& nlohmann_json_j, Type& nlohmann_json_t) { const Type nlohmann_json_default_obj{}; NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, __VA_ARGS__)) }

#define SERIALIZE_AS_STRING(type) template<> struct nlohmann::adl_serializer<type> : StringConvertibleSerializer<type> { };
#pragma endregion

#pragma region GW2Viewer.UI.ImGui
#include "dep/IconsFontAwesome6.h"

#define scoped auto _ = dear
#pragma endregion

#pragma region GW2Viewer.Utils.Async
#define CHECK_ASYNC do { if (!context || context->Cancelled) { context.reset(); return; } } while (false)
#define CHECK_SHARED_ASYNC do { if (!context || context->Cancelled) { context->Cancel(); return; } } while (false)
#pragma endregion

#pragma region GW2Viewer.Utils.Enum
#define DECLARE_ENUM_NAMES(type) template<> static constexpr std::pair<type const, char const* const> EnumNames<type, char>[]
#pragma endregion
