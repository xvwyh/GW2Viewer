export module GW2Viewer.Data.Pack;
import GW2Viewer.Common;
import std;

export namespace GW2Viewer::Data::Pack
{

#pragma pack(push, 1)
template<typename T, typename PointerType>
class PtrBase
{
    PointerType m_offset;

public:
    [[nodiscard]] T* get() const { return m_offset ? (T*)((byte*)this + m_offset) : nullptr; }
    [[nodiscard]] T& value() const { return *get(); }

    [[nodiscard]] T& operator*() const { return *get(); }
    [[nodiscard]] T* operator->() const { return get(); }
    [[nodiscard]] operator bool() const { return m_offset; }
};
template<typename T> using Ptr32 = PtrBase<T, int32>;
template<typename T> using Ptr64 = PtrBase<T, int64>;
template<typename T> using Ptr = Ptr64<T>;

template<typename CharT, typename PointerType>
class StringBase
{
    PtrBase<CharT, PointerType> m_pointer;

public:
    [[nodiscard]] bool empty() const { return m_pointer; }
    [[nodiscard]] CharT* data() const { return m_pointer.get(); }

    [[nodiscard]] operator bool() const { return empty(); }

    [[nodiscard]] operator std::basic_string_view<CharT>() const { return data(); }
};
template<typename PointerType = int64> using String = StringBase<char, PointerType>;
template<typename PointerType = int64> using WString = StringBase<wchar_t, PointerType>;
using String32 = String<int32>;
using String64 = String<int64>;
using WString32 = WString<int32>;
using WString64 = WString<int64>;

template<typename T, typename SizeType, typename PointerType>
class ArrayBase
{
    SizeType m_count;
    PtrBase<T, PointerType> m_pointer;

public:
    [[nodiscard]] SizeType size() const { return m_count; }
    [[nodiscard]] bool empty() const { return size() == 0; }
    [[nodiscard]] T* data() const { return m_pointer.get(); }
    [[nodiscard]] T* begin() const { return data(); }
    [[nodiscard]] T& front() const { return *begin(); }
    [[nodiscard]] T& back() const { return *(end() - 1); }
    [[nodiscard]] T* end() const { return begin() + size(); }
    [[nodiscard]] T& at(SizeType index) const { if (index >= size()) throw "index out of range"; return (*this)[index]; }

    [[nodiscard]] T& operator[](SizeType index) const { return *(begin() + index); }
};
template<typename T> using DwordArray32 = ArrayBase<T, uint32, int32>;
template<typename T> using DwordArray64 = ArrayBase<T, uint32, int64>;
template<typename T> using WordArray32 = ArrayBase<T, uint16, int32>;
template<typename T> using WordArray64 = ArrayBase<T, uint16, int64>;
template<typename T> using ByteArray32 = ArrayBase<T, byte, int32>;
template<typename T> using ByteArray64 = ArrayBase<T, byte, int64>;
template<typename T> using Array32 = DwordArray32<T>;
template<typename T> using Array64 = DwordArray64<T>;
template<typename T> using Array = Array64<T>;

template<typename SizeType, typename PointerType>
class TypedArrayBase
{
    SizeType m_count;
    PtrBase<byte /* unk */, PointerType> m_types;
    PtrBase<byte, PointerType> m_pointer;

public:
    [[nodiscard]] SizeType size() const { return m_count; }
    [[nodiscard]] bool empty() const { return size() == 0; }
    [[nodiscard]] byte* types() const { return m_types.get(); }
    [[nodiscard]] byte* data() const { return m_pointer.get(); }
};
template<typename T> using DwordTypedArray32 = TypedArrayBase<uint32, int32>;
template<typename T> using DwordTypedArray64 = TypedArrayBase<uint32, int64>;
template<typename T> using WordTypedArray32 = TypedArrayBase<uint16, int32>;
template<typename T> using WordTypedArray64 = TypedArrayBase<uint16, int64>;
template<typename T> using ByteTypedArray32 = TypedArrayBase<byte, int32>;
template<typename T> using ByteTypedArray64 = TypedArrayBase<byte, int64>;

class FileReference // actually should be a wchar_t[]
{
    uint16 m_low;
    uint16 m_high;
    uint16 m_term;

public:
    [[nodiscard]] uint32 GetFileID() const { return !m_term && m_high >= 0x100 && m_low >= 0x100 ? (m_high - 0x100) * 0xff00 + (m_low - 0xff) : 0; }
};
template<typename PointerType = int64>
class FileNameBase
{
    PtrBase<FileReference, PointerType> m_reference;

public:
    [[nodiscard]] FileReference const& GetFileReference() const { return *m_reference; }
    [[nodiscard]] uint32 GetFileID() const { return m_reference ? m_reference->GetFileID() : 0; }

    [[nodiscard]] operator FileReference() const { return GetFileReference(); }
};
using FileName = FileNameBase<int64>;
using FileName32 = FileNameBase<int32>;

template<typename PointerType = int64>
class Variant
{
    uint32 m_index;
    PtrBase<byte, PointerType> m_pointer;

public:
    [[nodiscard]] uint32 index() const { return m_index; }
    [[nodiscard]] byte* data() const { return m_pointer.get(); }
};
#pragma pack(pop)

}
