#pragma once
#include "Common.h"

#include <filesystem>
#include <map>
#include <unordered_map>
#include <vector>

namespace pf
{
template<typename PointerType> using GenericPtr = PtrBase<byte, PointerType>;
template<typename SizeType, typename PointerType> using GenericArray = ArrayBase<byte, SizeType, PointerType>;
template<typename SizeType, typename PointerType> using GenericPtrArray = ArrayBase<GenericPtr<PointerType>, SizeType, PointerType>;
template<typename SizeType, typename PointerType> using GenericTypedArray = TypedArrayBase<SizeType, PointerType>;
template<typename PointerType> using GenericDwordArray = GenericArray<uint32, PointerType>;
template<typename PointerType> using GenericWordArray = GenericArray<uint16, PointerType>;
template<typename PointerType> using GenericByteArray = GenericArray<byte, PointerType>;
template<typename PointerType> using GenericDwordPtrArray = GenericPtrArray<uint32, PointerType>;
template<typename PointerType> using GenericWordPtrArray = GenericPtrArray<uint16, PointerType>;
template<typename PointerType> using GenericBytePtrArray = GenericPtrArray<byte, PointerType>;
template<typename PointerType> using GenericDwordTypedArray = GenericTypedArray<uint32, PointerType>;
template<typename PointerType> using GenericWordTypedArray = GenericTypedArray<uint16, PointerType>;
template<typename PointerType> using GenericByteTypedArray = GenericTypedArray<byte, PointerType>;
template<typename PointerType> struct GenericFileReference : FileReference { private: uint16 padding; };
}
namespace pf::Layout
{
enum class UnderlyingTypes : uint16
{
    StructDefinition,
    InlineArray,
    DwordArray,
    DwordPtrArray,
    DwordTypedArray,
    Byte,
    Byte4,
    Double,
    Double2,
    Double3,
    Dword,
    FileName,
    Float,
    Float2,
    Float3,
    Float4,
    Ptr,
    Qword,
    WString,
    String,
    InlineStruct,
    Word,
    Byte16,
    Byte3,
    Dword2,
    Dword4,
    Word3,
    FileName2, // Same as FileName with slight differences (no "unable to resolve" error reporting and something else)
    Variant,
    InlineStruct2, // Has type error checking
    WordArray,
    WordPtrArray,
    WordTypedArray,
    ByteArray,
    BytePtrArray,
    ByteTypedArray,
    DwordID, // No endianness conversion
    QwordID, // No endianness conversion
    Max
};
enum class RealTypes : uint16
{
    Underlying,
    Unk1,
    Flags,
    Token,
};
struct Type;
struct Field
{
    std::string Name;
    UnderlyingTypes UnderlyingType;
    RealTypes RealType;
    uint32 ArraySize;
    Type const* ElementType;
    std::vector<Type const*> VariantElementTypes;

    [[nodiscard]] uint32 Size(bool x64) const
    {
        if (auto const size = CachedSize[x64])
            return size;
        return CachedSize[x64] = CalculateSize(x64);
    }
    [[nodiscard]] uint32 CalculateSize(bool x64) const;
    mutable std::array<uint32, 2> CachedSize { };
};
struct Type
{
    std::string Name;
    uint32 DeclaredSize;
    std::vector<Field> Fields;

    [[nodiscard]] uint32 Size(bool x64) const
    {
        if (auto const size = CachedSize[x64])
            return size;
        return CachedSize[x64] = CalculateSize(x64);
    }
    [[nodiscard]] uint32 CalculateSize(bool x64) const;
    mutable std::array<uint32, 2> CachedSize { };
};

extern bool g_loaded;
extern std::unordered_map<byte const*, Type> g_types;
extern std::map<std::string, std::map<uint32, Type const*>, std::less<>> g_chunks;
void ParsePackFileLayout(std::filesystem::path path);
}
