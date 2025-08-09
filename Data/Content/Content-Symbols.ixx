export module GW2Viewer.Data.Content:Symbols;
import :TypeInfo;
import GW2Viewer.Common;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Content;
import std;

export namespace GW2Viewer::Data::Content::Symbols
{

TypeInfo::SymbolType const* GetByName(std::string_view name);

template<typename T>
struct Integer : TypeInfo::SymbolType
{
    Integer(char const* name) : SymbolType(name) { }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override { return *(T const*)dataA <=> *(T const*)dataB; }
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return *(T const*)data; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override { return std::format("{}", *(T const*)data); }
    [[nodiscard]] uint32 Size() const override { return sizeof(T); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
template<typename T>
struct Number : TypeInfo::SymbolType
{
    Number(char const* name) : SymbolType(name) { }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override
    {
        auto const result = *(T const*)dataA <=> *(T const*)dataB;
        if (result == std::partial_ordering::less)
            return std::strong_ordering::less;
        if (result == std::partial_ordering::greater)
            return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override { return std::format("{}", *(T const*)data); }
    [[nodiscard]] uint32 Size() const override { return sizeof(T); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
template<typename T>
struct String : TypeInfo::SymbolType
{
    String(char const* name) : SymbolType(name) { }

    struct Struct
    {
        T const* Pointer;
        uint32 Hash;
    };
    static_assert(sizeof(Struct) == 8 + 4 + 4);
    static constexpr bool IsWide = std::is_same_v<T, wchar_t>;
    [[nodiscard]] static Struct const& GetStruct(byte const* data) { return *(Struct const*)data; }
    [[nodiscard]] static std::conditional_t<IsWide, std::wstring_view, std::string_view> GetStringView(byte const* data) { if (auto const str = GetStruct(data).Pointer) return str; return { }; }
    [[nodiscard]] static auto GetString(byte const* data) { return std::conditional_t<IsWide, std::wstring, std::string> { GetStringView(data) }; }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override;
    [[nodiscard]] uint32 Alignment() const override { return sizeof(Struct::Pointer); }
    [[nodiscard]] uint32 Size() const override { return sizeof(Struct::Pointer) + sizeof(Struct::Hash); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
template<typename T>
struct StringPointer : TypeInfo::SymbolType
{
    StringPointer(char const* name) : SymbolType(name) { }

    [[nodiscard]] String<T> const* GetTargetSymbolType() const { std::string_view name = Name; name.remove_suffix(1); return (String<T> const*)GetByName(name); }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(typename String<T>::Struct*); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct Color : TypeInfo::SymbolType
{
    std::array<byte, 4> Swizzle;

    Color(char const* name, std::array<byte, 4> swizzle) : SymbolType(name), Swizzle(swizzle) { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return *(uint32 const*)data; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(uint32); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
template<typename T, size_t N>
struct Point : TypeInfo::SymbolType
{
    Point(char const* name) : SymbolType(name) { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return *(T const*)data; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(T) * N; }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct GUID : TypeInfo::SymbolType
{
    GUID() : SymbolType("GUID") { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override;
    [[nodiscard]] std::optional<uint32> GetIcon(byte const* data) const override;
    [[nodiscard]] std::optional<ContentObject*> GetMap(byte const* data) const override;
    [[nodiscard]] bool IsContent() const override { return true; }
    [[nodiscard]] std::optional<ContentObject*> GetContent(byte const* data) const override;
    [[nodiscard]] bool IsInline() const override { return false; }
    [[nodiscard]] uint32 Size() const override { return sizeof(GW2Viewer::GUID); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct Token32 : TypeInfo::SymbolType
{
    Token32() : SymbolType("Token32") { }

    [[nodiscard]] static auto GetDecoded(byte const* data) { return ((GW2Viewer::Token32 const*)data)->GetString(); }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return *(uint32 const*)data; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override { return GetDecoded(data).data(); }
    [[nodiscard]] uint32 Size() const override { return sizeof(uint32); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct Token64 : TypeInfo::SymbolType
{
    Token64() : SymbolType("Token64") { }

    [[nodiscard]] static auto GetDecoded(byte const* data) { return ((GW2Viewer::Token64 const*)data)->GetString(); }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return *(uint64 const*)data; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override { return GetDecoded(data).data(); }
    [[nodiscard]] uint32 Size() const override { return sizeof(uint64); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct StringID : TypeInfo::SymbolType
{
    StringID() : SymbolType("StringID") { }

    [[nodiscard]] static uint32 GetStringID(byte const* data) { return *(uint32 const*)data; }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return *(uint32 const*)data; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(uint32); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct FileID : TypeInfo::SymbolType
{
    FileID() : SymbolType("FileID") { }

    [[nodiscard]] static uint32 GetFileID(byte const* data) { return *(uint32 const*)data; }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return *(uint32 const*)data; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override { return std::format("FileID: {}", GetFileID(data)); }
    [[nodiscard]] std::optional<uint32> GetIcon(byte const* data) const override { return GetFileID(data); }
    [[nodiscard]] uint32 Alignment() const override { return sizeof(wchar_t*); }
    [[nodiscard]] uint32 Size() const override { return sizeof(uint32); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct RawPointerT : TypeInfo::SymbolType
{
    RawPointerT(char const* name = "T*") : SymbolType(name) { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return (TypeInfo::Condition::ValueType)*GetPointer(data); }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override { return { }; }
    [[nodiscard]] std::optional<byte const*> GetPointer(byte const* data) const override { return *(byte const* const*)data; }
    [[nodiscard]] uint32 Size() const override { return sizeof(void*); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct ContentPointer : RawPointerT
{
    ContentPointer() : RawPointerT("Content*") { }

    [[nodiscard]] std::string GetDisplayText(byte const* data) const override;
    [[nodiscard]] std::optional<uint32> GetIcon(byte const* data) const override;
    [[nodiscard]] std::optional<ContentObject*> GetMap(byte const* data) const override;
    [[nodiscard]] bool IsContent() const override { return true; }
    [[nodiscard]] std::optional<ContentObject*> GetContent(byte const* data) const override;
    [[nodiscard]] bool IsInline() const override { return false; }
    [[nodiscard]] uint32 Size() const override { return sizeof(ContentObject*); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct ArrayT : TypeInfo::SymbolType
{
    ArrayT(char const* name = "Array<T>") : SymbolType(name) { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override { return { }; }
    [[nodiscard]] bool IsArray() const override { return true; }
    [[nodiscard]] std::optional<uint32> GetArrayCount(byte const* data) const override { return *(uint32 const*)(data + sizeof(byte*)); }
    [[nodiscard]] std::optional<byte const*> GetPointer(byte const* data) const override { return *(byte const* const*)data; }
    [[nodiscard]] uint32 Alignment() const override { return sizeof(void*); }
    [[nodiscard]] uint32 Size() const override { return sizeof(void*) + sizeof(uint32); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct ArrayContent : ArrayT
{
    ArrayContent() : ArrayT("Array<Content>") { }

    [[nodiscard]] bool IsContent() const override { return true; }
    [[nodiscard]] std::optional<ContentObject*> GetContent(byte const* data) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(ContentPointer*) + sizeof(uint32); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct ParamValue : TypeInfo::SymbolType
{
    ParamValue() : SymbolType("ParamValue") { }

    struct Struct
    {
        GW2Viewer::Content::EContentTypes ContentType;
        union
        {
            byte* Content;
            struct { int32 X, Y; } Integer;
            struct { float X, Y, Z; } Number;
            struct { wchar_t* String; uint32 Hash; } String;
            GW2Viewer::Token32 Token32;
            GW2Viewer::Token64 Token64;
            byte Raw[24];
        };
        GW2Viewer::GUID GUID;
    };
    static_assert(sizeof(Struct) == 4 + 4 + 24 + 16);
    [[nodiscard]] static Struct const& GetStruct(byte const* data) { return *(Struct const*)data; }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override;
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override;
    [[nodiscard]] std::optional<uint32> GetIcon(byte const* data) const override;
    [[nodiscard]] std::optional<ContentObject*> GetMap(byte const* data) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(Struct); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};
struct ParamDeclare : TypeInfo::SymbolType
{
    ParamDeclare() : SymbolType("ParamDeclare") { }

    struct Struct
    {
        String<wchar_t>::Struct Name;
        ParamValue::Struct Value;
    };
    static_assert(sizeof(Struct) == 8 + 4 + 4 + 4 + 4 + 24 + 16);
    [[nodiscard]] static Struct const& GetStruct(byte const* data) { return *(Struct const*)data; }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(byte const* data) const override;
    [[nodiscard]] std::string GetDisplayText(byte const* data) const override { return { }; }
    [[nodiscard]] std::optional<uint32> GetIcon(byte const* data) const override;
    [[nodiscard]] std::optional<ContentObject*> GetMap(byte const* data) const override;
    [[nodiscard]] uint32 Alignment() const override { return sizeof(void*); }
    [[nodiscard]] uint32 Size() const override { return sizeof(Struct); }
    void Draw(byte const* data, TypeInfo::Symbol& symbol) const override;
};

std::vector<TypeInfo::SymbolType const*>& GetTypes();

}
