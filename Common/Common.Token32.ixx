export module GW2Viewer.Common.Token32;
import GW2Viewer.Common;
import std;
import <boost/container/small_vector.hpp>;

export class Token32
{
    static constexpr std::string_view alphabet = "abcdefghiklmnopvrstuwxy";
    uint32 m_data;

public:
    Token32() { }
    Token32(uint32 data) : m_data(data) { }
    Token32(std::string_view string) : Token32(FromString(string)) { }
    Token32(char const* string) : Token32(std::string_view(string)) { }

    [[nodiscard]] bool empty() const { return !m_data; }
    [[nodiscard]] auto GetString() const
    {
        boost::container::small_vector<char, 16> decoded;
        if (uint32 token = m_data)
        {
            if (token -= 0x30000000)
            {
                while (token)
                {
                    decoded.emplace_back(alphabet[token % 23]);
                    token /= 23;
                }
            }
        }
        decoded.emplace_back('\0');
        return decoded;
    }
    [[nodiscard]] static uint32 FromString(std::string_view string)
    {
        uint32 token = 0;
        uint32 factor = 1;

        for (char const c : string)
        {
            if (auto const pos = alphabet.find(c); pos != std::string_view::npos)
                token += pos * factor;
            else
                return 0;

            factor *= 23;
        }

        return token + 0x30000000;
    }
};
