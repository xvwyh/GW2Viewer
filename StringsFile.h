#pragma once
#include "Common.h"

#include "Utils.h"

#include <string>
#include <format>

enum class EncryptionStatus;
enum
{
    TERM_FINAL = 0,
    TERM_INTERMEDIATE = 1,
    CONCAT_CODED = 2,
    CONCAT_LITERAL = 3,

    SPAN_TYPE_FIXUP = 0,
    FIXUP_TYPE_PLURAL = 1,

    FIXUP_TYPE_ARTICLE = 3,
    FIXUP_TYPE_ESCAPE = 4,

    FIXUP_ESCAPE_AUTO_DIGIT_SEPARATOR = 0x782D2CF2
};
enum TEXTPARAM
{
    TEXTPARAM_END = 0,
    TEXTPARAM_NUM1,
    TEXTPARAM_NUM2,
    TEXTPARAM_NUM3,
    TEXTPARAM_NUM4,
    TEXTPARAM_NUM5,
    TEXTPARAM_NUM6,
    TEXTPARAM_STR1_LITERAL,
    TEXTPARAM_STR2_LITERAL,
    TEXTPARAM_STR3_LITERAL,
    TEXTPARAM_STR4_LITERAL,
    TEXTPARAM_STR5_LITERAL,
    TEXTPARAM_STR6_LITERAL,
    TEXTPARAM_STR1_CODED,
    TEXTPARAM_STR2_CODED,
    TEXTPARAM_STR3_CODED,
    TEXTPARAM_STR4_CODED,
    TEXTPARAM_STR5_CODED,
    TEXTPARAM_STR6_CODED,
};

void LoadStringsFiles(class Archive& archive, class ProgressBarContext& progress);
bool WipeStringCache(uint32 stringID);
std::pair<std::wstring const*, EncryptionStatus> GetString(uint32 stringID);
std::pair<std::wstring const*, EncryptionStatus> GetNormalizedString(uint32 stringID);

inline void FormatTo(std::wstring& string) { }
template<typename T, typename... Args>
void FormatTo(std::wstring& string, TEXTPARAM param, T&& value, Args&&... args)
{
    using Type = std::decay_t<T>;
    switch (param)
    {
        case TEXTPARAM_END:
            return;
        case TEXTPARAM_NUM1:
        case TEXTPARAM_NUM2:
        case TEXTPARAM_NUM3:
        case TEXTPARAM_NUM4:
        case TEXTPARAM_NUM5:
        case TEXTPARAM_NUM6:
            if constexpr (std::integral<Type> || std::floating_point<Type>)
                replace_all(string, std::format(L"%num{}%", 1 + param - TEXTPARAM_NUM1), std::format(L"{}", std::forward<T>(value)));
            break;
        case TEXTPARAM_STR1_LITERAL:
        case TEXTPARAM_STR2_LITERAL:
        case TEXTPARAM_STR3_LITERAL:
        case TEXTPARAM_STR4_LITERAL:
        case TEXTPARAM_STR5_LITERAL:
        case TEXTPARAM_STR6_LITERAL:
            if constexpr (!(std::integral<Type> || std::floating_point<Type>))
                replace_all(string, std::format(L"%str{}%", 1 + param - TEXTPARAM_STR1_LITERAL), std::forward<T>(value));
            break;
        case TEXTPARAM_STR1_CODED:
        case TEXTPARAM_STR2_CODED:
        case TEXTPARAM_STR3_CODED:
        case TEXTPARAM_STR4_CODED:
        case TEXTPARAM_STR5_CODED:
        case TEXTPARAM_STR6_CODED:
            if constexpr (std::integral<Type>)
                if (value)
                    if (auto const paramString = GetString(std::forward<T>(value)).first)
                        replace_all(string, std::format(L"%str{}%", 1 + param - TEXTPARAM_STR1_CODED), *paramString);
            break;
    }
    FormatTo(string, std::forward<Args>(args)...);
}

namespace detail {
void FixupString(std::wstring& string);
}

template<typename... Args>
std::wstring FormatString(uint32 stringID, Args&&... args)
{
    if (auto format = GetString(stringID).first)
    {
        std::wstring result = *format;
        FormatTo(result, std::forward<Args>(args)...);
        replace_all(result, L"%%", L"%");
        detail::FixupString(result);
        return result;
    }
    return { };
}
