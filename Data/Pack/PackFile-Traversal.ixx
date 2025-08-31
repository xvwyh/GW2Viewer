export module GW2Viewer.Data.Pack.PackFile:Traversal;
import :Layout;
import :PackFile;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Data.Pack;
import GW2Viewer.UI.ImGui;
import GW2Viewer.Utils.Scan;
import <boost/container/small_vector.hpp>;
import <experimental/generator>;

namespace GW2Viewer::Data::Pack::Layout::Traversal
{

std::map<uint32, Type const*> const* GetChunk(std::string_view name);

}

export namespace GW2Viewer::Data::Pack::Layout::Traversal
{

class FieldIterator
{
public:
    using iterator_category = std::input_iterator_tag;
    using value_type = FieldIterator;
    using difference_type = ptrdiff_t;

    template<typename T>
    using Generator = std::experimental::generator<T>;
    using Bounds = std::pair<FieldIterator, FieldIterator>;
    using TypeFieldIterator = decltype(Type::Fields)::const_iterator;

    static Bounds MakeFieldIteratorBounds(byte const* ptr, bool x64, Type const& type)
    {
        return { { ptr, x64, type, type.Fields.begin() }, { ptr, x64, type, type.Fields.end(), true } };
    }
    static Bounds MakeArrayIteratorBounds(byte const* ptr, FieldIterator const& field, uint32 size)
    {
        return { { ptr, field, size, { } }, { ptr, field, size, { }, true } };
    }

    FieldIterator() = default;
    FieldIterator(byte const* ptr, bool x64, Type const& type, TypeFieldIterator field, bool end = { }) : FieldIterator(end, ptr, x64, &type, field, { }, { }, { }) { }
    //FieldIterator(byte const* ptr, bool x64, Type const* type, TypeFieldIterator field, uint32 arraySize, uint32 arrayIndex = 0) : FieldIterator(ptr, x64, type, field, field->ElementType, arraySize, arrayIndex) { }
    FieldIterator(byte const* ptr, FieldIterator const& field, uint32 arraySize, uint32 arrayIndex = { }, bool end = { }) : FieldIterator(end, ptr, field.m_x64, field.m_type, field.m_fieldItr, field.m_fieldItr->ElementType, arraySize, arrayIndex) { }

    template<typename T>
    [[nodiscard]] auto const& Get(uint32 index = 0) const { return ((T const*)m_ptr)[index]; }
    [[nodiscard]] auto GetPointer() const { return m_ptr; }
    [[nodiscard]] bool IsArrayIterator() const { return m_arrayElementType; }
    [[nodiscard]] auto const& GetArrayType() const { return *m_arrayElementType; }
    [[nodiscard]] auto GetArrayIndex() const { return m_arrayIndex; }
    [[nodiscard]] auto GetArraySize() const { return m_arraySize; }
    [[nodiscard]] auto IsArrayEmpty() const { return GetArraySize() == 0; }
    [[nodiscard]] auto const& GetType() const { return *m_type; }
    [[nodiscard]] auto const& GetField() const { return *m_fieldItr; }

    [[nodiscard]] auto GetPtrTarget() const
    {
        switch (GetField().UnderlyingType)
        {
            case UnderlyingTypes::Variant:
                return m_x64 ? Get<Variant<int64>>().data() : Get<Variant<int32>>().data();
            case UnderlyingTypes::DwordPtrArray:
            case UnderlyingTypes::WordPtrArray:
            case UnderlyingTypes::BytePtrArray:
            case UnderlyingTypes::DwordTypedArray: // TODO: Test
            case UnderlyingTypes::WordTypedArray: // TODO: Test
            case UnderlyingTypes::ByteTypedArray: // TODO: Test
                if (IsArrayIterator())
            case UnderlyingTypes::Ptr:
                return m_x64 ? Get<GenericPtr<int64>>().get() : Get<GenericPtr<int32>>().get();
            default: throw std::exception("FieldIterator::GetPtrTarget() called for a field of non-pointer type");
        }
    }
    [[nodiscard]] auto GetTargetFields() const
    {
        switch (GetField().UnderlyingType)
        {
            case UnderlyingTypes::InlineArray:
            case UnderlyingTypes::DwordArray:
            case UnderlyingTypes::WordArray:
            case UnderlyingTypes::ByteArray:
                if (!IsArrayIterator()) throw std::exception("FieldIterator::GetTargetFields() for fields of array types can only be called from an iterator pointing to the array field, not an iterator that's iterating through the array");
            case UnderlyingTypes::InlineStruct:
            case UnderlyingTypes::InlineStruct2:
                return MakeFieldIteratorBounds(GetPointer(), m_x64, *GetField().ElementType);
            case UnderlyingTypes::DwordPtrArray:
            case UnderlyingTypes::WordPtrArray:
            case UnderlyingTypes::BytePtrArray:
            case UnderlyingTypes::DwordTypedArray: // TODO: Test
            case UnderlyingTypes::WordTypedArray: // TODO: Test
            case UnderlyingTypes::ByteTypedArray: // TODO: Test
            case UnderlyingTypes::Ptr:
                return MakeFieldIteratorBounds(GetPtrTarget(), m_x64, *GetField().ElementType);
            case UnderlyingTypes::Variant:
                return MakeFieldIteratorBounds(GetPtrTarget(), m_x64, *GetField().VariantElementTypes[m_x64 ? Get<Variant<int64>>().index() : Get<Variant<int32>>().index()]);
            default: throw std::exception("FieldIterator::GetTargetFields() called for a field of non-target type");
        }
    }
    [[nodiscard]] auto GetArrayElements() const
    {
        switch (GetField().UnderlyingType)
        {
            case UnderlyingTypes::InlineArray: return MakeArrayIteratorBounds(GetPointer(), *this, m_x64 ? Get<GenericDwordArray<int64>>().size() : Get<GenericDwordArray<int32>>().size());
            case UnderlyingTypes::DwordArray:
            case UnderlyingTypes::DwordPtrArray: return MakeArrayIteratorBounds(m_x64 ? Get<GenericDwordArray<int64>>().data() : Get<GenericDwordArray<int32>>().data(), *this, m_x64 ? Get<GenericDwordArray<int64>>().size() : Get<GenericDwordArray<int32>>().size());
            case UnderlyingTypes::WordArray:
            case UnderlyingTypes::WordPtrArray: return MakeArrayIteratorBounds(m_x64 ? Get<GenericWordArray<int64>>().data() : Get<GenericWordArray<int32>>().data(), *this, m_x64 ? Get<GenericWordArray<int64>>().size() : Get<GenericWordArray<int32>>().size());
            case UnderlyingTypes::ByteArray:
            case UnderlyingTypes::BytePtrArray: return MakeArrayIteratorBounds(m_x64 ? Get<GenericByteArray<int64>>().data() : Get<GenericByteArray<int32>>().data(), *this, m_x64 ? Get<GenericByteArray<int64>>().size() : Get<GenericByteArray<int32>>().size());
            case UnderlyingTypes::DwordTypedArray: return MakeArrayIteratorBounds(m_x64 ? Get<GenericDwordTypedArray<int64>>().data() : Get<GenericDwordTypedArray<int32>>().data(), *this, m_x64 ? Get<GenericDwordTypedArray<int64>>().size() : Get<GenericDwordTypedArray<int32>>().size());
            case UnderlyingTypes::WordTypedArray: return MakeArrayIteratorBounds(m_x64 ? Get<GenericWordTypedArray<int64>>().data() : Get<GenericWordTypedArray<int32>>().data(), *this, m_x64 ? Get<GenericWordTypedArray<int64>>().size() : Get<GenericWordTypedArray<int32>>().size());
            case UnderlyingTypes::ByteTypedArray: return MakeArrayIteratorBounds(m_x64 ? Get<GenericByteTypedArray<int64>>().data() : Get<GenericByteTypedArray<int32>>().data(), *this, m_x64 ? Get<GenericByteTypedArray<int64>>().size() : Get<GenericByteTypedArray<int32>>().size());
            default: throw std::exception("FieldIterator::GetArrayElements() called for a field of non-array type");
        }
    }

    [[nodiscard]] operator FieldIterator() const { return *this; }
    template<typename T>
    [[nodiscard]] operator T const*() const { return (T const*)GetPointer(); }
    template<typename T>
    [[nodiscard]] operator T*() const { return (T*)GetPointer(); }
    template<std::integral T>
    [[nodiscard]] operator T() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::Byte: return Get<byte>();
            case UnderlyingTypes::Word: return Get<uint16>();
            case UnderlyingTypes::Dword: return Get<uint32>();
            case UnderlyingTypes::Qword: return Get<uint64>();
            case UnderlyingTypes::FileName:
            case UnderlyingTypes::FileName2: return m_x64 ? Get<FileNameBase<int64>>().GetFileID() : Get<FileNameBase<int32>>().GetFileID();
            default: throw std::exception("FieldIterator::operator T()<std::integral> called for a field of non-integral type");
        }
    }
    template<std::floating_point T>
    [[nodiscard]] operator T() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::Float: return Get<float>();
            case UnderlyingTypes::Double: return Get<double>();
            default: throw std::exception("FieldIterator::operator T()<std::floating_point> called for a field of non-integral type");
        }
    }
    template<typename T> requires std::integral<T> || std::floating_point<T>
    [[nodiscard]] operator std::array<T, 2>() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::Dword2: return { (T)Get<uint32>(0), (T)Get<uint32>(1) };
            case UnderlyingTypes::Float2: return { (T)Get<float>(0), (T)Get<float>(1) };
            case UnderlyingTypes::Double2: return { (T)Get<double>(0), (T)Get<double>(1) };
            default: throw std::exception("FieldIterator::operator T()<[2]> called for a field of non-array type");
        }
    }
    [[nodiscard]] operator ImVec2() const { std::array<float, 2> array = *this; return { array[0], array[1] }; }
    template<typename T> requires std::integral<T> || std::floating_point<T>
    [[nodiscard]] operator std::array<T, 3>() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::Byte3: return { (T)Get<byte>(0), (T)Get<byte>(1), (T)Get<byte>(2) };
            case UnderlyingTypes::Word3: return { (T)Get<uint16>(0), (T)Get<uint16>(1), (T)Get<uint16>(2) };
            case UnderlyingTypes::Float3: return { (T)Get<float>(0), (T)Get<float>(1), (T)Get<float>(2) };
            case UnderlyingTypes::Double3: return { (T)Get<double>(0), (T)Get<double>(1), (T)Get<double>(2) };
            default: throw std::exception("FieldIterator::operator T()<[3]> called for a field of non-array type");
        }
    }
    template<typename T> requires std::integral<T> || std::floating_point<T>
    [[nodiscard]] operator std::array<T, 4>() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::Byte4: return { (T)Get<byte>(0), (T)Get<byte>(1), (T)Get<byte>(2), (T)Get<byte>(3) };
            case UnderlyingTypes::Dword4: return { (T)Get<uint32>(0), (T)Get<uint32>(1), (T)Get<uint32>(2), (T)Get<uint32>(3) };
            case UnderlyingTypes::Float4: return { (T)Get<float>(0), (T)Get<float>(1), (T)Get<float>(2), (T)Get<float>(3) };
            default: throw std::exception("FieldIterator::operator T()<[4]> called for a field of non-array type");
        }
    }
    [[nodiscard]] operator ImVec4() const { std::array<float, 4> array = *this; return { array[0], array[1], array[2], array[3] }; }
    [[nodiscard]] operator GUID() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::Byte16: return Get<GUID>();
            default: throw std::exception("FieldIterator::operator GUID() called for a field of non-GUID type");
        }
    }
    [[nodiscard]] operator Token32() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::Dword:
                //if (GetElementField().RealType == RealTypes::Token || GetField().RealType == RealTypes::Token)
                    return Get<Token32>();
                throw std::exception("FieldIterator::operator Token32() called for a field of non-Token32 type");
            default: throw std::exception("FieldIterator::operator Token32() called for a field of non-dword type");
        }
    }
    [[nodiscard]] operator Token64() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::Qword:
                //if (GetElementField().RealType == RealTypes::Token || GetField().RealType == RealTypes::Token)
                    return Get<Token64>();
                throw std::exception("FieldIterator::operator Token64() called for a field of non-Token64 type");
            default: throw std::exception("FieldIterator::operator Token64() called for a field of non-qword type");
        }
    }
    [[nodiscard]] operator std::string() const { return std::string((std::string_view)*this); }
    [[nodiscard]] operator std::string_view() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::String: return m_x64 ? Get<String<int64>>().data() : Get<String<int32>>().data();
            default: throw std::exception("FieldIterator::operator std::string_view() called for a field of non-string type");
        }
    }
    [[nodiscard]] operator std::wstring() const { return std::wstring((std::wstring_view)*this); }
    [[nodiscard]] operator std::wstring_view() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::WString: return m_x64 ? Get<WString<int64>>().data() : Get<WString<int32>>().data();
            default: throw std::exception("FieldIterator::operator std::wstring_view() called for a field of non-wstring type");
        }
    }
    [[nodiscard]] operator FileReference() const
    {
        switch (GetElementField().UnderlyingType)
        {
            case UnderlyingTypes::FileName:
            case UnderlyingTypes::FileName2: return m_x64 ? Get<FileNameBase<int64>>().GetFileReference() : Get<FileNameBase<int32>>().GetFileReference();
            default: throw std::exception("FieldIterator::operator FileReference() called for a field of non-filename type");
        }
    }

    template<typename T = FieldIterator>
    [[nodiscard]] Generator<T> Query(std::string_view path) const;
    template<typename T = FieldIterator>
    [[nodiscard]] T QuerySingle(std::string_view path) const;
    FieldIterator operator[](std::string_view path) const { return QuerySingle(path); }
    FieldIterator operator[](char const* path) const { return (*this)[std::string_view(path)]; }
    FieldIterator operator[](FieldIterator const& itr) const { return (*this)[(uint32)itr]; }
    FieldIterator operator[](uint32 index) const
    {
        auto array = IsArrayIterator() ? *this : GetArrayElements().first;
        if (!array.IsArrayIterator())
            throw std::exception("FieldIterator::operator[](index) yielded a non-array iterator");
        if (array.GetArrayIndex())
            throw std::exception("FieldIterator::operator[](index) yielded an array iterator that was already indexed");
        return { array.GetPointer(), array, array.GetArraySize(), index };
    }

    [[nodiscard]] bool operator==(FieldIterator const& itr) const = default;
    [[nodiscard]] FieldIterator operator*() const { return *this; }
    [[nodiscard]] byte const* operator&() const { return GetPointer(); }
    [[nodiscard]] operator bool() const { return m_ptr && (IsArrayIterator() ? GetArrayIndex() < GetArraySize() : m_fieldItr != GetType().Fields.end()); }
    FieldIterator operator++(int) { auto const temp = *this; ++*this; return temp; }
    FieldIterator& operator++()
    {
        if (*this)
        {
            if (IsArrayIterator())
            {
                ++m_arrayIndex;
                m_ptr += GetArrayType().Size(m_x64);
            }
            else
                m_ptr += m_fieldItr++->Size(m_x64);
        }
        return *this;
    }
    [[nodiscard]] auto begin() const { return IsArrayIterator() ? FieldIterator(GetPointer(), *this, GetArraySize() - GetArrayIndex(), GetArrayIndex()) : GetArrayElements().first; }
    [[nodiscard]] auto end() const { return IsArrayIterator() ? FieldIterator(GetPointer(), *this, GetArraySize() - GetArrayIndex(), GetArrayIndex(), true) : GetArrayElements().second; }
    [[nodiscard]] auto size() const { return GetArraySize(); }
    [[nodiscard]] auto data() const { return GetPointer(); }

private:

    [[nodiscard]] auto GetElementSize() const { return (IsArrayIterator() ? m_arrayElementType : m_type)->Size(m_x64); }
    [[nodiscard]] auto const& GetElementField() const { return IsArrayIterator() ? m_arrayElementType->Fields.front() : *m_fieldItr; }

    FieldIterator(bool end, byte const* ptr, bool x64, Type const* type, TypeFieldIterator field, Type const* arrayElementType, uint32 arraySize, uint32 arrayIndex) :
        m_x64(x64),
        m_type(type),
        m_fieldItr(field),
        m_arrayElementType(arrayElementType),
        m_arraySize(arraySize),
        m_arrayIndex(end ? m_arraySize : std::min(arrayIndex, m_arraySize)),
        m_ptr(ptr + (IsArrayIterator() ? m_arrayIndex : (uint32)end) * GetElementSize()) { }

    bool m_x64 { };
    Type const* m_type { };
    TypeFieldIterator m_fieldItr { };
    Type const* m_arrayElementType { };
    uint32 m_arraySize { };
    uint32 m_arrayIndex { };
    byte const* m_ptr { };
};

template<typename T, typename Result>
concept FieldSearcher = requires(T const a, FieldIterator const& field)
{
    { a.CanCheck(field) } -> std::same_as<bool>;
    { a.CanReturn(field) } -> std::same_as<bool>;
    { a.Return(field) } -> std::convertible_to<Result>;
    { a.Deeper() } -> std::convertible_to<T>;
};
template<typename T, FieldSearcher<T> Searcher>
FieldIterator::Generator<T> QueryPackFileFieldsImpl(FieldIterator::Bounds bounds, Searcher searcher)
{
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
    {
        if (!searcher.CanCheck(itr))
            continue;

        if (searcher.CanReturn(itr))
        {
            co_yield searcher.Return(itr);
            continue;
        }

        switch (itr.IsArrayIterator() ? UnderlyingTypes::InlineStruct : itr.GetField().UnderlyingType)
        {
            case UnderlyingTypes::InlineArray:
            case UnderlyingTypes::DwordArray:
            case UnderlyingTypes::WordArray:
            case UnderlyingTypes::ByteArray:
                for (auto&& result : QueryPackFileFieldsImpl<T>(itr.GetArrayElements(), searcher))
                    co_yield result;
                break;
            case UnderlyingTypes::DwordPtrArray:
            case UnderlyingTypes::WordPtrArray:
            case UnderlyingTypes::BytePtrArray:
                for (auto const& pointers : itr)
                    if (pointers.GetPtrTarget())
                        for (auto&& result : QueryPackFileFieldsImpl<T>(pointers.GetTargetFields(), searcher.Deeper()))
                            co_yield result;
                break;
            case UnderlyingTypes::DwordTypedArray: 
            case UnderlyingTypes::WordTypedArray: 
            case UnderlyingTypes::ByteTypedArray: 
                std::terminate(); // TODO: Not yet implemented
            case UnderlyingTypes::Ptr:
            case UnderlyingTypes::Variant:
                if (itr.GetPtrTarget())
            case UnderlyingTypes::InlineStruct:
            case UnderlyingTypes::InlineStruct2:
                for (auto&& result : QueryPackFileFieldsImpl<T>(itr.GetTargetFields(), searcher.Deeper()))
                    co_yield result;
                break;
            default: throw std::exception("QueryPackFileFieldsImpl() called for a field of non-traversable type");
        }
    }
}

template<typename T = FieldIterator>
FieldIterator::Generator<T> QueryFields(FieldIterator::Bounds bounds, std::string_view path)
{
    struct PathPart
    {
        std::string_view Name;
        bool ArrayElements = false;
        std::optional<uint32> ArrayIndex;
    };
    boost::container::small_vector<PathPart, 5> parts;
    for (auto const& part : std::views::split(path, std::string_view(".")))
    {
        std::string_view name { part };
        if (auto const subscriptIndex = name.find('['); subscriptIndex != std::string_view::npos)
        {
            auto const subscriptEndIndex = name.find(']', subscriptIndex);
            if (subscriptEndIndex == std::string_view::npos)
                throw std::exception("QueryFields() called with a malformed path parameter: array subscript not closed");

            std::string_view index = name.substr(subscriptIndex + 1, subscriptEndIndex - subscriptIndex - 1);
            name = name.substr(0, subscriptIndex);
            if (index.empty())
                parts.emplace_back(name, true);
            else if (auto result = Utils::Scan::Single<uint32>(index))
                parts.emplace_back(name, true, *result);
            else
                throw std::exception("QueryFields() called with a malformed path parameter: failed to parse array index");
        }
        else
            parts.emplace_back(name);
    }

    struct PathSearcher
    {
        std::span<PathPart> Path;
        [[nodiscard]] bool CanCheck(FieldIterator const& field) const
        {
            if (field.GetField().Name != Path.front().Name)
                return false;
            if (field.IsArrayIterator())
            {
                if (!Path.front().ArrayElements)
                    return false;
                if (Path.front().ArrayIndex && field.GetArrayIndex() != *Path.front().ArrayIndex)
                    return false;
            }
            return true;
        }
        [[nodiscard]] bool CanReturn(FieldIterator const& field) const { return Path.size() == 1 && field.IsArrayIterator() == Path.front().ArrayElements; }
        [[nodiscard]] T Return(FieldIterator const& field) const { return field.operator T(); }
        [[nodiscard]] PathSearcher Deeper() const { return { Path.subspan(1) }; }
    };
    for (auto&& result : QueryPackFileFieldsImpl<T>(bounds, PathSearcher { parts }))
        co_yield result;
}

template<typename T = FieldIterator>
FieldIterator::Generator<T> QueryFields(PackFile const& file, PackFileChunk const& chunk, std::string_view path)
{
    if (auto const chunkInfo = GetChunk(std::string_view { (char const*)&chunk.Header.Magic, 4 }))
        if (auto const itrChunkVersion = chunkInfo->find(chunk.Header.Version); itrChunkVersion != chunkInfo->end())
            for (auto&& result : QueryFields<T>(FieldIterator::MakeFieldIteratorBounds(chunk.Data, file.Header.Is64Bit, *itrChunkVersion->second), path))
                co_yield result;
}

template<typename T = FieldIterator>
FieldIterator::Generator<T> QueryFields(PackFile const& file, fcc chunkMagic, std::string_view path)
{
    for (auto const& chunk : file)
        if (chunk.Header.Magic == chunkMagic)
            for (auto&& result : QueryFields<T>(file, chunk, path))
                co_yield result;
}

template<typename T>
FieldIterator::Generator<T> FieldIterator::Query(std::string_view path) const
{
    for (auto&& result : QueryFields<T>(GetTargetFields(), path))
        co_yield result;
}

template<typename T>
T FieldIterator::QuerySingle(std::string_view path) const
{
    for (auto&& result : Query<T>(path))
        return result;
    return { };
}

struct QueryChunk
{
    PackFile const& File;
    PackFileChunk const& Chunk;

    template<typename T = FieldIterator>
    [[nodiscard]] FieldIterator::Generator<T> Query(std::string_view path) const
    {
        for (auto&& result : QueryFields<T>(File, Chunk, path))
            co_yield result;
    }
    template<typename T = FieldIterator>
    [[nodiscard]] T QuerySingle(std::string_view path) const
    {
        for (auto&& result : Query<T>(path))
            return result;
        return { };
    }
    FieldIterator operator[](std::string_view path) const { return QuerySingle(path); }
    FieldIterator operator[](char const* path) const { return (*this)[std::string_view(path)]; }
    operator bool() const { return true; }
};

}
