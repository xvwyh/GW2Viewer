export module GW2Viewer.Data.Media.Text.Format;
import GW2Viewer.Common;
import GW2Viewer.Data.Game;
import GW2Viewer.Utils.String;
import std;

void FixupString(std::wstring& string)
{
    if (!string.contains(L'['))
        return;

    std::wstring result;
    result.resize(string.size());
    auto writeDest = result.data();
    uint32 sex = 2;
    bool plural = false;
    auto const pStart = string.data();
    auto const pEnd = pStart + string.size();
    auto copyFrom = pStart;
    auto p = pStart;
    auto write = [&]
    {
        if (copyFrom != p)
        {
            writeDest += string.copy(writeDest, p - copyFrom, copyFrom - pStart);
            copyFrom = p;
        }
    };
    for (; p < pEnd; ++p)
    {
        auto const c = *p;
        if (c == L'[')
        {
            write();
            auto const fixupEnd = string.find(']', std::distance(pStart, p + 1));
            if (fixupEnd == std::wstring::npos)
                break;
            std::wstring_view fixup { p + 1, (size_t)std::distance(p + 1, &string[fixupEnd]) };
            if (fixup == L"null")
                ;
            else if (fixup == L"lbracket")
                *writeDest++ = L'[';
            else if (fixup == L"rbracket")
                *writeDest++ = L']';
            else if (fixup == L"plur")
                plural = true;
            else if (fixup == L"m")
                sex = 0;
            else if (fixup == L"f")
                sex = 1;
            else if (fixup == L"s")
            {
                if (plural)
                    *writeDest++ = L's';
            }
            else if (fixup.starts_with(L"pl:\"") && fixup.ends_with('"'))
            {
                if (plural)
                {
                    writeDest = &result[std::wstring_view { result.data(), writeDest }.find_last_of(L' ') + 1];
                    std::fill(writeDest, result.data() + result.size(), L'\0');
                    writeDest += fixup.copy(writeDest, fixup.size() - 5, 4);
                }
            }
            else if (fixup.starts_with(L"f:\"") && fixup.ends_with('"'))
            {
                if (sex == 1)
                {
                    writeDest = &result[std::wstring_view { result.data(), writeDest }.find_last_of(L' ') + 1];
                    std::fill(writeDest, result.data() + result.size(), L'\0');
                    writeDest += fixup.copy(writeDest, fixup.size() - 5, 4);
                }
            }
            // an
            // the
            // nosep
            // b
            else
                continue;

            p = &string[fixupEnd];
            copyFrom = p + 1;
        }
    }
    write();

    result.resize(std::distance(result.data(), writeDest));
    string = std::move(result);
}

export namespace Data::Media::Text
{

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
                Utils::String::ReplaceAll(string, std::format(L"%num{}%", 1 + param - TEXTPARAM_NUM1), std::format(L"{}", std::forward<T>(value)));
            break;
        case TEXTPARAM_STR1_LITERAL:
        case TEXTPARAM_STR2_LITERAL:
        case TEXTPARAM_STR3_LITERAL:
        case TEXTPARAM_STR4_LITERAL:
        case TEXTPARAM_STR5_LITERAL:
        case TEXTPARAM_STR6_LITERAL:
            if constexpr (!(std::integral<Type> || std::floating_point<Type>))
                Utils::String::ReplaceAll(string, std::format(L"%str{}%", 1 + param - TEXTPARAM_STR1_LITERAL), std::forward<T>(value));
            break;
        case TEXTPARAM_STR1_CODED:
        case TEXTPARAM_STR2_CODED:
        case TEXTPARAM_STR3_CODED:
        case TEXTPARAM_STR4_CODED:
        case TEXTPARAM_STR5_CODED:
        case TEXTPARAM_STR6_CODED:
            if constexpr (std::integral<Type>)
                if (value)
                    if (auto const paramString = G::Game.Text.Get(std::forward<T>(value)).first)
                        Utils::String::ReplaceAll(string, std::format(L"%str{}%", 1 + param - TEXTPARAM_STR1_CODED), *paramString);
            break;
    }
    FormatTo(string, std::forward<Args>(args)...);
}

template<typename... Args>
std::wstring FormatString(uint32 stringID, Args&&... args)
{
    if (auto format = G::Game.Text.Get(stringID).first)
    {
        std::wstring result = *format;
        FormatTo(result, std::forward<Args>(args)...);
        Utils::String::ReplaceAll(result, L"%%", L"%");
        FixupString(result);
        return result;
    }
    return { };
}

}
