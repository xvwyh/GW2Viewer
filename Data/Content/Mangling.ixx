export module GW2Viewer.Data.Content.Mangling;
import GW2Viewer.Common;
import GW2Viewer.Common.Hash;
import std;
import <boost/archive/iterators/base64_from_binary.hpp>;
import <boost/archive/iterators/binary_from_base64.hpp>;
import <boost/archive/iterators/transform_width.hpp>;

export namespace Data::Content
{
enum
{
    MANGLE_FULL_NAME_BUFFER_SIZE = 12,
};

uint64 MangleToNumber(std::wstring_view name, Hasher& hasher)
{
    std::array<byte, 32> hash;
    //picosha2::hash256(std::string_view { (char const*)name.data(), 2 * name.length() }, hash);
    hasher.SHA256.init();
    hasher.SHA256.process((char const*)name.data(), (char const*)name.data() + 2 * name.length());
    hasher.SHA256.finish();
    hasher.SHA256.get_hash_bytes(hash.begin(), hash.end());

    auto pHash = (uint32 const*)hash.data();
    uint64 fnv = 0xCBF29CE484222325ULL;
    for (int i = 0; i < 8; ++i)
    {
        uint32 chunk = *pHash++;
        for (int j = 0; j < 4; ++j)
        {
            fnv = (fnv ^ chunk) * 0x100000001B3ULL;
            chunk >>= 8;
        }
    }
    return fnv;
}
uint32 DemangleToNumber(std::wstring_view mangledName)
{
    uint32 fnv = 0;
    using namespace boost::archive::iterators;
    using iterator = transform_width<binary_from_base64<decltype(mangledName)::const_iterator>, 8, 6>;
    std::copy(iterator(mangledName.begin()), iterator(mangledName.end()), (byte*)&fnv);
    return fnv;
}
void Mangle(std::wstring_view name, wchar_t* dest, size_t chars, Hasher& hasher)
{
    uint64 const fnv = MangleToNumber(name, hasher);
    using namespace boost::archive::iterators;
    using iterator = base64_from_binary<transform_width<byte const*, 6, 8>>;
    std::copy_n(iterator((byte const*)&fnv), chars - 1, dest);
    dest[chars - 1] = L'\0';
}
void MangleFullName(std::wstring_view name, wchar_t* dest, uint32 chars, Hasher& hasher)
{
    assert(chars >= MANGLE_FULL_NAME_BUFFER_SIZE);
    if (auto const pos = name.find_last_of(L'.'); pos != std::wstring_view::npos)
    {
        Mangle(name.substr(0, pos), dest, 6, hasher);
        dest[5] = '.';
        Mangle(name.substr(pos + 1), dest + 6, 6, hasher);
    }
    else
        Mangle(name, dest, 6, hasher);
}
std::wstring MangleFullName(std::wstring_view name)
{
    Hasher hasher;
    std::wstring mangled(MANGLE_FULL_NAME_BUFFER_SIZE, L'\0');
    MangleFullName(name, mangled.data(), mangled.length() + 1, hasher);
    mangled.resize(wcslen(mangled.c_str()));
    return mangled;
}

}
