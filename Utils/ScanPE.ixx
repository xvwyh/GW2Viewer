module;
#include <Windows.h>

export module GW2Viewer.Utils.ScanPE;
import GW2Viewer.Common;
import std;

export namespace GW2Viewer::Utils::ScanPE
{

struct Scanner
{
    struct Section
    {
        std::span<byte const> Bounds { };
        uint32 VirtualAddress = 0;
        uint64 ImageBase = 0;
        byte const* ModuleBase = nullptr;

        auto size() const { return Bounds.size(); }
        auto begin() const { return Bounds.data(); }
        auto end() const { return Bounds.data() + Bounds.size(); }

        [[nodiscard]] operator bool() const { return !Bounds.empty(); }

        [[nodiscard]] bool Contains(void const* ptr) const { return ptr >= Bounds.data() && ptr <= Bounds.data() + Bounds.size(); }
        [[nodiscard]] bool Valid(void const* ptr) const { return !ptr || Contains(ptr); }

        [[nodiscard]] std::optional<uint64> GetSectionOffset(void const* ptr) const { if (Contains(ptr)) return std::distance(Bounds.data(), (byte const*)ptr); return { }; }
        [[nodiscard]] std::optional<uint64> GetRVA(void const* ptr) const { if (auto offset = GetSectionOffset(ptr)) return VirtualAddress + *offset; return { }; }
        [[nodiscard]] std::optional<uint64> GetVA(void const* ptr) const { if (auto rva = GetRVA(ptr)) return ImageBase + *rva; return { }; }
    };

    std::span<byte const> File;
    std::span<byte const> Module;
    Section text, rdata, data;

    explicit Scanner(std::filesystem::path const& path)
    {
        std::ifstream fileStream(path, std::ios::binary);
        if (!fileStream.is_open())
            return;

        auto const fileSize = file_size(path);
        std::unique_ptr<byte[]> const fileContents = std::make_unique<byte[]>(fileSize);
        if (!fileStream.read((char*)fileContents.get(), fileSize))
            return;

        File = { fileContents.get(), fileSize };

        auto const dos = (IMAGE_DOS_HEADER const*)&File[0];
        auto const nt = (IMAGE_NT_HEADERS const*)&File[dos->e_lfanew];

        byte* module = (byte*)VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        Module = { module, nt->OptionalHeader.SizeOfImage };

        std::fill_n(module, Module.size(), 0);
        std::copy_n(&File[0], nt->OptionalHeader.SizeOfHeaders, module);

        for (auto const& section : std::span(IMAGE_FIRST_SECTION(nt), nt->FileHeader.NumberOfSections))
        {
            std::copy_n(&File[section.PointerToRawData], std::min(section.SizeOfRawData, section.Misc.VirtualSize), &module[section.VirtualAddress]);

            static std::map<std::string_view, Section(Scanner::*)> contextSectionMapping
            {
                { ".text",  &Scanner::text  },
                { ".rdata", &Scanner::rdata },
                { ".data",  &Scanner::data  },
            };
            if (auto const itr = contextSectionMapping.find((char const*)section.Name); itr != contextSectionMapping.end())
            {
                auto& target = this->*itr->second;
                target.Bounds = { &module[section.VirtualAddress], section.SizeOfRawData };
                target.VirtualAddress = section.VirtualAddress;
                target.ImageBase = nt->OptionalHeader.ImageBase;
                target.ModuleBase = Module.data();
            }
        }

        if (auto const& relocationDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]; relocationDirectory.VirtualAddress && relocationDirectory.Size)
        {
            auto p = &module[relocationDirectory.VirtualAddress];
            auto end = p + relocationDirectory.Size;
            while (p < end)
            {
                auto const* relocation = (IMAGE_BASE_RELOCATION const*)p;
                if (!relocation->SizeOfBlock)
                    break;

                for (auto const entry : std::span((uint16*)&relocation[1], (relocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16)))
                    if (entry >> 12 == IMAGE_REL_BASED_DIR64)
                        *(uint64*)&module[relocation->VirtualAddress + (entry & 0x0FFF)] += (uint64)module - nt->OptionalHeader.ImageBase;

                p += relocation->SizeOfBlock;
            }
        }
    }
    ~Scanner()
    {
        VirtualFree((void*)Module.data(), 0, MEM_RELEASE);
    }

    [[nodiscard]] static byte const* GetTargetFromOffset32(byte const* ptrToOffset) { return ptrToOffset + sizeof(uint32) + *(uint32 const*)ptrToOffset; }
};

}
