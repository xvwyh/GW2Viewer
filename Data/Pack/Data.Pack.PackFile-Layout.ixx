export module GW2Viewer.Data.Pack.PackFile:Layout;
import GW2Viewer.Common;
import GW2Viewer.Data.Pack;
import std;

export namespace Data::Pack
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

export namespace Data::Pack::Layout
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

constexpr std::array<uint32, (uint32)UnderlyingTypes::Max> UnderlyingTypeSizes { { 0, 0, 0, 0, 0, 1, 4, 8, 16, 24, 4, 0, 4, 8, 12, 16, 0, 8, 0, 0, 0, 2, 16, 3, 8, 16, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 8 } };

template<template<typename PointerType> typename PackFileType>
constexpr auto GetPackFileTypeSize(bool x64) { return x64 ? sizeof(PackFileType<int64>) : sizeof(PackFileType<int32>); }

static_assert(GetPackFileTypeSize<GenericPtr>(false) == 4);
static_assert(GetPackFileTypeSize<GenericPtr>(true) == 8);

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
    [[nodiscard]] uint32 CalculateSize(bool x64) const
    {
        return std::ranges::fold_left(Fields, 0u, [x64](uint32 size, Field const& field) { return size + field.Size(x64); });
    }
    mutable std::array<uint32, 2> CachedSize { };
};

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

}
