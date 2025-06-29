
#define STATIC(variable) decltype(variable) variable

#define DECLARE_ENUM_NAMES(type) template<> static constexpr std::pair<type const, char const* const> EnumNames<type, char>[]
