#include "Utils.h"

#include <codecvt>
#include <locale>

#include <Windows.h>

thread_local static std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;

std::string to_utf8(std::wstring_view wstr)
{
    return convert.to_bytes(wstr.data(), wstr.data() + wstr.length());
}

std::wstring from_utf8(std::string_view str)
{
    return convert.from_bytes(str.data(), str.data() + str.length());
}

std::wstring from_utf8(std::u8string_view str)
{
    return convert.from_bytes((char const*)str.data(), (char const*)str.data() + str.length());
}

std::wstring to_wstring(std::string_view str)
{
    return { str.begin(), str.end() };
}

scoped_seh_exception_handler scoped_seh_exception_handler::Create()
{
    return { [](unsigned int u, _EXCEPTION_POINTERS* pointers)
    {
        if (u == 0xC0000005)
            throw std::exception(std::format("Access violation at 0x{:x}", (uintptr_t)pointers->ExceptionRecord->ExceptionAddress).c_str());
        throw std::exception(std::format("SEH exception 0x{:X}", u).c_str());
    } };
}
scoped_seh_exception_handler::scoped_seh_exception_handler(_se_translator_function handler) noexcept : old(_set_se_translator(handler)) { }
scoped_seh_exception_handler::~scoped_seh_exception_handler() noexcept { _set_se_translator(old); }

