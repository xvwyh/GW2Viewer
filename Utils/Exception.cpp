module;
#include "Windows.h"

module GW2Viewer.Utils.Exception;

Utils::Exception::SEHandler Utils::Exception::SEHandler::Create()
{
    return { [](unsigned int u, _EXCEPTION_POINTERS* pointers)
    {
        if (u == 0xC0000005)
            throw std::exception(std::format("Access violation at 0x{:x}", (uintptr_t)pointers->ExceptionRecord->ExceptionAddress).c_str());
        throw std::exception(std::format("SEH exception 0x{:X}", u).c_str());
    } };
}
