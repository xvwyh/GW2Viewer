export module GW2Viewer.Tasks.ContentObjectDisplayFormat;
import GW2Viewer.Common;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Utils.Encoding;
import std;
import <boost/container/small_vector.hpp>;
import <gsl/util>;
#include "Macros.h"

export namespace GW2Viewer::Tasks
{

struct ContentObjectDisplayFormat
{
    [[nodiscard]] char const* GetDefault() const;
    [[nodiscard]] char const* GetSyntaxHelp() const;
    [[nodiscard]] char const* GetMarkupHelp() const;

    [[nodiscard]] auto GetRecursionGuard(Data::Content::ContentObject const& object) const
    {
        auto const recursion = std::ranges::contains(tls_recursionPrevention, &object);
        if (!recursion)
            tls_recursionPrevention.emplace_back(&object);

        return std::make_pair(recursion ? L"<c=#F00>RECURSION</c>" : nullptr, gsl::finally([&tls_recursionPrevention = tls_recursionPrevention, recursion, &object]
        {
            if (!recursion)
            {
                auto const itr = std::ranges::find_last(tls_recursionPrevention.begin(), tls_recursionPrevention.end(), &object);
                if (itr.empty())
                    std::terminate();
                tls_recursionPrevention.erase(itr.begin());
            }
        }));
    }

    [[nodiscard]] std::string Process(Data::Content::ContentObject const& object, std::string_view displayFormat) const;

private:
    [[nodiscard]] std::string Process(Data::Content::ContentObject const& object, char const*& pFormat, char const* pFormatEnd, bool inTernary = false) const;

    inline static thread_local boost::container::small_vector<Data::Content::ContentObject const*, 100> tls_recursionPrevention;
};

}

export namespace GW2Viewer::G::Tasks { GW2Viewer::Tasks::ContentObjectDisplayFormat ContentObjectDisplayFormat; }

module :private;

namespace GW2Viewer::Tasks
{

#define CODE(str) "<c=#FFFF><code>" #str "</code></c>"
char const* ContentObjectDisplayFormat::GetDefault() const { return "{@name}"; }
char const* ContentObjectDisplayFormat::GetSyntaxHelp() const
{
    return   CODE(format) " = " CODE(text) " | " CODE({expression})
        "\n" CODE(expression) " ="
        "\n   | " CODE(path) " - Prints the first non-empty occurrence of " CODE(path)
        "\n   | " CODE(patha|pathb|pathc) " - Prints first non-empty occurrence of either " CODE(patha) " or " CODE(pathb) " or " CODE(pathc)
        "\n   | " CODE(path?format) " - Prints " CODE(format) " if " CODE(path) " yielded a non-empty result, or nothing otherwise. " CODE(format) " can contain " CODE({expression}) "s"
        "\n   | " CODE(path?trueformat:falseformat) " - Prints " CODE(trueformat) " if " CODE(path) " yielded a non-empty result, or " CODE(falseformat) " otherwise"
        "\n" CODE(path) " ="
        "\n   | " CODE(symbol) " - Yields the first non-empty occurrence of the symbol " CODE(symbol)
        "\n   | " CODE(path->symbol) " - Yields the first non-empty occurrence of the symbol " CODE(symbol) " matched by following the path " CODE(path)
        "\n   | " CODE(refpath->..->relpath) " - Finds the first reference matched by " CODE(refpath) " and yields the first non-empty occurrence of the symbol matched by following the path " CODE(relpath) " relative to the symbol's containing symbol"
        "\n   | " CODE(refpath->..) " - Yields the symbol of the first reference matched by " CODE(refpath) ". Needed for accessing symbols relative to the reference, read below"
        "\n   | " CODE(path[]) " - Yields all the symbols matched by following the path " CODE(path) ", separated by default with <c=#FFFF><code>, </code></c> (or " CODE() " if the path ends with " CODE(->@icon) ")"
        "\n   | " CODE(path[]sep...) " - Yields all the symbols matched by following the path " CODE(path) ", separated by " CODE(sep)
        "\n" CODE(symbol) " ="
        "\n   | " CODE(field) " - Yields the contents of the field with the name " CODE(field)
        "\n   | " CODE(@name) " - Yields the name of the content object created without using the \"Display Format\" system"
        "\n   | " CODE(@display) " - Yields the name of the content object created using the \"Display Format\" system"
        "\n   | " CODE(@path) " - Yields the full namespace path to the content object"
        "\n   | " CODE(@type) " - Yields the content object's type name"
        "\n   | " CODE(@icon) " - Yields the content object's icon in " CODE(\\<img=fileid/>) " format"
        "\n   | " CODE(@iconname) " - Yields the " CODE(@icon) " and " CODE(@name) " combined"
        "\n   | " CODE(@icondisplay) " - Yields the " CODE(@icon) " and " CODE(@display) " combined"
        "\n   | " CODE(@map) " - Yields the content object's map name"
        "\n   | " CODE(@ref) " - Yields the content object's incoming references"
        "\n   | " CODE(@ref<Type>) " - Yields the content object's incoming references of type " CODE(Type)
        "\n"
        "\nExamples: "
        "\n   " CODE(Coin: {Coin} Karma: {Karma})
        "\n   " CODE(Used by {@ref[] and ...})
        "\n   " CODE(Sells {Item->@icondisplay[]})
        "\n   " CODE({Bit? out of 1}{Counter? out of {Counter->ValueMax}})
        "\n   " CODE({@ref<TypeA>|@ref<TypeB>?referenced from TypeA or TypeB:not referenced from TypeA and TypeB})
        "\n"
        "\nWhen inside " CODE(@ref) ", you can address fields relative to where the reference was located."
        "\nTo accomplish this, first specify the path to the reference, and then use " CODE(..) " fields to backtrack from that reference to adjacent or parent fields."
        "\nExamples: "
        "\n   " CODE({@ref<VendorServiceTab>->Item->Limit->..->Name}) " yields the " CODE(Item->Name) " field adjacent to the " CODE(Item->Limit) " field inside VendorServiceTab that contains the reference to the current content object."
        "\n   " CODE({@ref<VendorServiceTab>->LimitProgress->..?has limit}) " yields the text \"has limit\" if the referencing VendorServiceTab contains the reference to the current content object in its " CODE(LimitProgress) " symbol."
        "\n"
        "\nSyntax can be escaped by prepending it with " CODE(\\\\) "."
        "\nLine breaks are ignored, use them freely for readability."
        "\nUse " CODE(\\n) " to deliberately print a line break."
        "\n"
        "\n<c=#F88>" ICON_FA_TRIANGLE_EXCLAMATION " Take care when creating recursive paths. Some of them are detected and will display an error, but others might hang or crash the application. Save before experimenting.</c>"
    ;
}
char const* ContentObjectDisplayFormat::GetMarkupHelp() const
{
    return   CODE(\\<c=color>text\\</c>) " - Alters the color of " CODE(text)
        "\n   " CODE(color) " ="
        "\n      | " CODE(#<c=#8>A</c>) " - Multiples the alpha by " CODE(<c=#8>AA</c>) " (" CODE(#4) " makes it <c=#4>25% opaque</c>, " CODE(#8) " - <c=#8>50%</c>, " CODE(#F) " leaves alpha unchanged)"
        "\n      | " CODE(#W<c=#8>A</c>) " - Changes the color to " CODE(WWWWWW) " and alpha to " CODE(<c=#8>AA</c>) " (" CODE(#0C) " makes it <c=#0C>75% opaque black</c>)"
        "\n      | " CODE(#<c=#F88>R</c><c=#8F8>G</c><c=#88F>B</c>) " - Changes the color to " CODE(<c=#F88>RR</c><c=#8F8>GG</c><c=#88F>BB</c>) " without changing the alpha"
        "\n      | " CODE(#<c=#F88>R</c><c=#8F8>G</c><c=#88F>B</c><c=#8>A</c>) " - Changes the color to " CODE(<c=#F88>RR</c><c=#8F8>GG</c><c=#88F>BB</c>) " and alpha to " CODE(<c=#8>AA</c>)
        "\n      | " CODE(#<c=#F88>RR</c><c=#8F8>GG</c><c=#88F>BB</c>) " - Changes the color to " CODE(<c=#F88>RR</c><c=#8F8>GG</c><c=#88F>BB</c>) " without changing the alpha"
        "\n      | " CODE(#<c=#F88>RR</c><c=#8F8>GG</c><c=#88F>BB</c><c=#8>AA</c>) " - Changes the color to " CODE(<c=#F88>RR</c><c=#8F8>GG</c><c=#88F>BB</c>) " and alpha to " CODE(<c=#8>AA</c>)
        "\n" CODE(\\<b>text\\</b>) " - Makes the " CODE(text) " <b>bold</b>"
        "\n" CODE(\\<code>text\\</code>) " - Changes the font of " CODE(text) " to <code>monospace</code>"
        "\n" CODE(\\<img=fileid/>) " - Embeds an image from the archive file with ID " CODE(fileid) " (" CODE(\\<img=102372/>) " embeds <c=#FFFF><img=102372/></c>)"
        "\n" CODE(\\<nosel>text\\</nosel>) " - Hides " CODE(text) " when an input field is focused for text selection"
        "\n"
        "\nMarkup can be escaped by prepending it with " CODE(\\\\)  "."
    ;
}
#undef CODE

std::string ContentObjectDisplayFormat::Process(Data::Content::ContentObject const& content, std::string_view displayFormat) const
{
    auto pFormat = displayFormat.data();
    auto name = Process(content, pFormat, pFormat + displayFormat.size());
    name.shrink_to_fit();
    return name;
}
std::string ContentObjectDisplayFormat::Process(Data::Content::ContentObject const& content, char const*& pFormat, char const* pFormatEnd, bool inTernary) const
{
    std::string display;
    display.reserve(std::distance(pFormat, pFormatEnd) * 5);

    auto formatRemainder = [&](uint32 offset = 0) -> std::string_view { return { pFormat + offset, pFormatEnd }; };
    auto readUntil = [&](std::string_view chars)
    {
        auto result = formatRemainder();
        if (auto const charPos = result.find_first_of(chars); charPos != std::string::npos)
            result = { pFormat, charPos };
        pFormat += result.size();
        return result;
    };

    while (pFormat < pFormatEnd)
    {
        switch (*pFormat)
        {
            case '{': // Parse expression
            {
                ++pFormat;
                struct Alternative
                {
                    std::string_view Path;
                    bool Array = false;
                    std::string_view ArraySeparator = Path.ends_with("->@icon") ? "" : ", ";
                };
                boost::container::small_vector<Alternative, 5> alternatives;
                std::optional<std::string> trueText, falseText;

            parseName:
                auto& alternative = alternatives.emplace_back(readUntil("}|[?\n"));

                while (pFormat < pFormatEnd)
                {
                    switch (*pFormat)
                    {
                        case '}': // End of expression
                            goto endParseExpression;
                        case '|': // Parse alternative
                            ++pFormat;
                            goto parseName;
                        case '[': // Parse array
                            if (pFormat[1] == ']')
                            {
                                alternative.Array = true;
                                pFormat += 2;
                                // Parse array separator
                                if (auto const separatorEnd = formatRemainder().find("..."); separatorEnd != std::string::npos)
                                {
                                    alternative.ArraySeparator = { pFormat, pFormat + separatorEnd };
                                    pFormat += alternative.ArraySeparator.size() + 3;
                                }
                                continue;
                            }
                            // Unexpected character
                            break;
                        case '?': // Ternary expression - parse true part
                            ++pFormat;
                            trueText = Process(content, pFormat, pFormatEnd, true);
                            switch (*pFormat)
                            {
                                case ':': // Parse false part
                                    ++pFormat;
                                    falseText = Process(content, pFormat, pFormatEnd, true);
                                    switch (*pFormat)
                                    {
                                        case '}':
                                        case '|':
                                        case '[':
                                        case '\0':
                                            continue;
                                        default: // Unexpected character
                                            break;
                                    }
                                    break;
                                case '}':
                                case '|':
                                case '[':
                                case '\0':
                                    continue;
                                default: // Unexpected character
                                    break;
                            }
                            break;
                        case '\n': // Ignore newline
                            ++pFormat;
                            continue;
                    }
                    // Unexpected character
                    display.append(std::format("<c=#F00>{}</c>", *pFormat));
                    ++pFormat;
                }

            endParseExpression:
                if (pFormat >= pFormatEnd)
                {
                    display.append("<c=#F00>" ICON_FA_EMPTY_SET "</c>");
                    return display;
                }
                ++pFormat;

                // Process expression
                bool exists = false;
                bool first = true;
                bool wasEncrypted = false;
                static auto const encryptedText = Data::Encryption::GetStatusText(Data::Encryption::Status::Encrypted);
                for (auto const& alternative : alternatives)
                {
                    if (alternative.Path.empty())
                        continue;

                    for (auto& result : Data::Content::QuerySymbolData(content, alternative.Path))
                    {
                        exists = true;

                        std::string value;
                        auto const symbolType = result.Symbol.GetType();
                        auto const resultContent = symbolType->GetContent(result).value_or(nullptr);
                        if (resultContent == &content)
                            value = "<c=#F00>RECURSION</c>";
                        else if (auto text = symbolType->GetDisplayText(result); !text.empty())
                            value = std::move(text);
                        else if (resultContent)
                            value = Utils::Encoding::ToUTF8(resultContent->GetDisplayName(false, true));

                        if (value == encryptedText)
                        {
                            wasEncrypted = true;
                            continue;
                        }

                        if (!value.empty())
                        {
                            // Append the true part of the ternary expression
                            if (trueText)
                            {
                                display.append(*trueText);
                                goto endProcessExpression;
                            }
                            // Append array separator
                            if (alternative.Array && !first)
                                display.append(alternative.ArraySeparator);
                            first = false;
                            // Append the yielded result
                            display.append(wasEncrypted ? encryptedText + value : value);
                            // Stop processing expression if we're not printing a whole array
                            if (!alternative.Array)
                                break;
                        }
                    }
                    // Stop processing alternate fields if one of them yielded a result
                    if (!first)
                        break;
                }
                // Append the result of the ternary expression (true part if anything matched even if no result was yielded, false part if nothing matched)
                if (first)
                    if (auto const& text = exists ? trueText : falseText)
                        display.append(*text);

            endProcessExpression:
                break;
            }
            case ':': // Process possible end of true part of the ternary expression or append literal character
                if (inTernary)
                    return display; // End parsing current part of the ternary expression
                // Append literal character
                display.push_back(*pFormat);
                ++pFormat;
                break;
            case '}': // Process end of expression
                if (inTernary)
                    return display; // End parsing current part of the ternary expression
                // Unexpected end of expression
                display.append(std::format("<c=#F00>{}</c>", *pFormat));
                ++pFormat;
                break;
            case '\\': // Process escape sequence
                switch (pFormat[1])
                {
                    case '\0': // Unexpected EOL after the start of the escape sequence
                        display.append(std::format("<c=#F00>{}</c>", *pFormat));
                        ++pFormat;
                        break;
                    case 'n': // Append newline
                        display.push_back('\n');
                        pFormat += 2;
                        break;
                    default: // Append literal
                        display.push_back(pFormat[1]);
                        pFormat += 2;
                        break;
                }
                break;
            case '\n': // Ignore newline
                ++pFormat;
                break;
            default: // Append literal text
                display.append(readUntil("{:}\\\n"));
                break;
        }
    }
    return display;
}

}
