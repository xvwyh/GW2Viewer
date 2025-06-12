#include "PackFileLayout.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <ranges>

namespace pf::Layout
{
decltype(g_loaded) g_loaded { };
decltype(g_types) g_types { };
decltype(g_chunks) g_chunks { };

constexpr std::array<uint32, (uint32)UnderlyingTypes::Max> UnderlyingTypeSizes { { 0, 0, 0, 0, 0, 1, 4, 8, 16, 24, 4, 0, 4, 8, 12, 16, 0, 8, 0, 0, 0, 2, 16, 3, 8, 16, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 8 } };

template<template<typename PointerType> typename PackFileType>
constexpr auto GetPackFileTypeSize(bool x64) { return x64 ? sizeof(PackFileType<int64>) : sizeof(PackFileType<int32>); }

static_assert(GetPackFileTypeSize<GenericPtr>(false) == 4);
static_assert(GetPackFileTypeSize<GenericPtr>(true) == 8);

uint32 Field::CalculateSize(bool x64) const
{
    if (auto const primitiveSize = UnderlyingTypeSizes[(uint32)UnderlyingType])
        return primitiveSize;

    switch (UnderlyingType)
    {
        case UnderlyingTypes::InlineArray: return ElementType->Size(x64) * ArraySize;
        case UnderlyingTypes::DwordArray: return GetPackFileTypeSize<GenericDwordArray>(x64);
        case UnderlyingTypes::DwordPtrArray: return GetPackFileTypeSize<GenericDwordPtrArray>(x64);
        case UnderlyingTypes::DwordTypedArray: return GetPackFileTypeSize<GenericDwordTypedArray>(x64);
        case UnderlyingTypes::FileName:
        case UnderlyingTypes::FileName2: return GetPackFileTypeSize<FileNameBase>(x64);
        case UnderlyingTypes::Ptr: return GetPackFileTypeSize<GenericPtr>(x64);
        case UnderlyingTypes::WString: return GetPackFileTypeSize<WString>(x64);
        case UnderlyingTypes::String: return GetPackFileTypeSize<String>(x64);
        case UnderlyingTypes::InlineStruct: 
        case UnderlyingTypes::InlineStruct2: return ElementType->Size(x64);
        case UnderlyingTypes::Variant: return GetPackFileTypeSize<Variant>(x64);
        case UnderlyingTypes::WordArray: return GetPackFileTypeSize<GenericWordArray>(x64);
        case UnderlyingTypes::WordPtrArray: return GetPackFileTypeSize<GenericWordPtrArray>(x64);
        case UnderlyingTypes::WordTypedArray: return GetPackFileTypeSize<GenericWordTypedArray>(x64);
        case UnderlyingTypes::ByteArray: return GetPackFileTypeSize<GenericByteArray>(x64);
        case UnderlyingTypes::BytePtrArray: return GetPackFileTypeSize<GenericBytePtrArray>(x64);
        case UnderlyingTypes::ByteTypedArray: return GetPackFileTypeSize<GenericByteTypedArray>(x64);
        default: std::terminate();
    }
}
uint32 Type::CalculateSize(bool x64) const
{
    return std::ranges::fold_left(Fields, 0u, [x64](uint32 size, Field const& field) { return size + field.Size(x64); });
}

void ParsePackFileLayout(std::filesystem::path path)
{
    std::ifstream fileStream(path, std::ios::binary);
    if (!fileStream.is_open())
        return;

    auto const fileSize = file_size(path);
    std::unique_ptr<byte[]> const fileContents = std::make_unique<byte[]>(fileSize);
    if (!fileStream.read((char*)fileContents.get(), fileSize))
        return;

    std::unique_ptr<byte[]> const fileBackup = std::make_unique<byte[]>(fileSize);
    std::ranges::copy_n(fileContents.get(), fileSize, fileBackup.get());

    byte const* file = fileContents.get();

    struct SectionInfo
    {
        std::span<byte const> Bounds { };
        uint32 VirtualOffset = 0;

        [[nodiscard]] bool IsInside(byte const* ptr) const { return ptr >= Bounds.data() && ptr <= Bounds.data() + Bounds.size(); }
        [[nodiscard]] operator bool() const { return !Bounds.empty(); }
    } rdata, data;
    auto const dosHeaders = (IMAGE_DOS_HEADER const*)file;
    auto const exeHeaders = (IMAGE_NT_HEADERS const*)&file[dosHeaders->e_lfanew];
    uint64 exeBaseOffset = exeHeaders->OptionalHeader.ImageBase;
    for (auto exeSection = (IMAGE_SECTION_HEADER const*)&exeHeaders[1], exeSectionsEnd = &exeSection[exeHeaders->FileHeader.NumberOfSections]; exeSection != exeSectionsEnd; ++exeSection)
    {
        if (std::string_view((char const*)exeSection->Name) == ".rdata")
        {
            rdata.Bounds = { &file[exeSection->PointerToRawData], exeSection->SizeOfRawData };
            rdata.VirtualOffset = exeSection->VirtualAddress;
        }
        else if (std::string_view((char const*)exeSection->Name) == ".data")
        {
            data.Bounds = { &file[exeSection->PointerToRawData], exeSection->SizeOfRawData };
            data.VirtualOffset = exeSection->VirtualAddress;
        }
    }
    if (!rdata || !data)
        return;
    
    auto adjust = [&](auto*& pointer, SectionInfo const& section)
    {
        if (!pointer)
            return true;

        auto const backup = pointer;
        auto& ptr = (byte const*&)pointer;
        if (section.IsInside(ptr))
            return true;

        ptr -= exeBaseOffset;
        ptr -= section.VirtualOffset;
        ptr += (uintptr_t)section.Bounds.data();
        if (section.IsInside(ptr))
            return true;

        pointer = backup;
        return false;
    };
    auto readPointer = [&]<typename T>(byte const* address) -> T*
    {
        auto pointer = *(byte const**)address;
        return adjust(pointer, rdata) ? (T*)pointer : nullptr;
    };

    struct PackFileField
    {
        UnderlyingTypes UnderlyingType;
        RealTypes RealType;
        uint32 Unk;
        char const* Name;
        union
        {
            PackFileField* ElementFields;
            PackFileField** VariantElementFields;
            void(*PostProcessStruct)();
        };
        uint16 Size;
    };
    struct PackFileVersion
    {
        PackFileField* Fields;
        void* PostProcessFunction;
        void* Unk;
    };
    auto adjustFields = [&](PackFileField* fields, auto& adjustFields) -> Type const*
    {
        if (!fields)
            return nullptr;
        PackFileField* field = fields;
        while (true)
        {
            if (!adjust(field->Name, rdata))
                return nullptr;
            if (field->UnderlyingType != UnderlyingTypes::StructDefinition && !adjust(field->ElementFields, field->UnderlyingType == UnderlyingTypes::Variant ? data : rdata))
                return nullptr;
            if (field->UnderlyingType == UnderlyingTypes::StructDefinition)
                return &g_types.try_emplace((byte const*)fields,
                    field->Name,
                    field->Size,
                    std::vector { std::from_range,
                        std::span { fields, field }
                        | std::views::transform([&](PackFileField const& field) -> Field {
                            return {
                                field.Name,
                                field.UnderlyingType,
                                field.RealType,
                                field.Size,
                                field.UnderlyingType != UnderlyingTypes::Variant ? adjustFields(field.ElementFields, adjustFields) : nullptr,
                                field.UnderlyingType == UnderlyingTypes::Variant ? std::vector { std::from_range, std::span { field.VariantElementFields, field.Size } | std::views::transform([&](auto* fields) { adjust(fields, rdata); return adjustFields(fields, adjustFields); }) } : std::vector<Type const*> { },
                            };
                        })
                    }).first->second;
            ++field;
        }
    };

    for (auto p = rdata.Bounds.data(); p < rdata.Bounds.data() + rdata.Bounds.size(); p += sizeof(void*))
    {
        if (isalnum(p[0]) && isalnum(p[1]) && isalnum(p[2]) && (!p[3] || isalnum(p[3])))
        {
            uint32 const numVersions = *(uint32 const*)&p[4];
            if (!numVersions || numVersions > 100)
                continue;

            if (auto const versions = readPointer.operator()<PackFileVersion>(&p[8]))
            {
                std::ranges::copy_n(fileBackup.get(), fileSize, fileContents.get());

                for (uint32 versionNum = 0; versionNum < numVersions; ++versionNum)
                {
                    auto& version = versions[versionNum];
                    if (!version.Fields)
                        continue;
                    if (!adjust(version.Fields, rdata))
                        goto fail;
                    //if (!adjust(version.PostProcessFunction, text))
                    //    goto fail;

                    if (auto type = adjustFields(version.Fields, adjustFields))
                        g_chunks[std::string((char const*)p, p[3] ? 4 : 3)].try_emplace(versionNum, type);
                    else
                        goto fail;
                }
            }

            fail:;
        }
    }

    g_loaded = true;
}
}
