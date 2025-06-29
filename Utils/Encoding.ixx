export module GW2Viewer.Utils.Encoding;
import std;

thread_local static std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;

export namespace Utils::Encoding
{

std::string ToUTF8(std::wstring_view wstr)
{
    return convert.to_bytes(wstr.data(), wstr.data() + wstr.length());
}
std::wstring FromUTF8(std::string_view str)
{
    return convert.from_bytes(str.data(), str.data() + str.length());
}
std::wstring FromUTF8(std::u8string_view str)
{
    return convert.from_bytes((char const*)str.data(), (char const*)str.data() + str.length());
}
std::wstring ToWString(std::string_view str)
{
    return { str.begin(), str.end() };
}

}
