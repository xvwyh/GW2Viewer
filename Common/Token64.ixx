export module GW2Viewer.Common.Token64;
import GW2Viewer.Common;
import std;
import <cctype>;
import <boost/container/small_vector.hpp>;

export namespace GW2Viewer
{

class Token64
{
    uint64 m_data;

public:
    Token64() { }
    Token64(uint64 data) : m_data(data) { }
    Token64(std::string_view string) : Token64(FromString(string)) { }
    Token64(char const* string) : Token64(std::string_view(string)) { }

    [[nodiscard]] bool empty() const { return !m_data; }
    [[nodiscard]] auto GetString() const
    {
        boost::container::small_vector<char, 16> decoded;
        if (uint64 token = m_data)
        {
            uint64 const tokenNum = token >> 60;
            for (token &= 0xFFFFFFFFFFFFFFF; token; token >>= 5)
                decoded.emplace_back(token & 0x1F ? '`' + (token & 0x1F) : ' ');
            if (tokenNum)
            {
                decoded.emplace_back('0' + tokenNum / 10);
                decoded.emplace_back('0' + tokenNum % 10);
            }
        }
        decoded.emplace_back('\0');
        return decoded;
    }
    [[nodiscard]] static uint64 FromString(std::string_view string)
    {
        uint64 token = 0;
        int len = string.size();

        uint64 tokenNum = 0;
        if (len >= 2 && isdigit(string[len - 2]) && isdigit(string[len - 1]))
        {
            tokenNum = (string[len - 2] - '0') * 10 + (string[len - 1] - '0');
            len -= 2;
        }

        for (int i = len - 1; i >= 0; --i)
        {
            char const c = string[i];
            token <<= 5;
            token |= c == ' ' ? 0 : c - '`';
        }

        token |= tokenNum << 60;

        return token;
    }
};

}
