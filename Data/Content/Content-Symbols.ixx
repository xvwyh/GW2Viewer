export module GW2Viewer.Data.Content:Symbols;
import :TypeInfo;
import GW2Viewer.Common;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Content;
import std;

using Context = GW2Viewer::Data::Content::TypeInfo::Context;

export namespace GW2Viewer::Data::Content::Symbols
{

TypeInfo::SymbolType const* GetByName(std::string_view name);

template<typename T>
struct Integer : TypeInfo::SymbolType
{
    Integer(char const* name) : SymbolType(name) { }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override { return *(T const*)dataA <=> *(T const*)dataB; }
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return context.Data<T>(); }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(T); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return context.Data<T>(); }
    void Draw(Context const& context) const override;
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
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return std::format("{}", context.Data<T>()); }
    [[nodiscard]] uint32 Size() const override { return sizeof(T); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return context.Data<T>(); }
    void Draw(Context const& context) const override;
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
    [[nodiscard]] static Struct const& GetStruct(Context const& context) { return context.Data<Struct>(); }
    [[nodiscard]] static std::conditional_t<IsWide, std::wstring_view, std::string_view> GetStringView(byte const* data) { if (auto const str = GetStruct(data).Pointer) return str; return { }; }
    [[nodiscard]] static std::conditional_t<IsWide, std::wstring_view, std::string_view> GetStringView(Context const& context) { if (auto const str = GetStruct(context).Pointer) return str; return { }; }
    [[nodiscard]] static auto GetString(Context const& context) { return std::conditional_t<IsWide, std::wstring, std::string> { GetStringView(context) }; }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] uint32 Alignment() const override { return sizeof(Struct::Pointer); }
    [[nodiscard]] uint32 Size() const override { return sizeof(Struct::Pointer) + sizeof(Struct::Hash); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return GetString(context); }
    void Draw(Context const& context) const override;
};
template<typename T>
struct StringPointer : TypeInfo::SymbolType
{
    StringPointer(char const* name) : SymbolType(name) { }

    [[nodiscard]] String<T> const* GetTargetSymbolType() const { std::string_view name = Name; name.remove_suffix(1); return (String<T> const*)GetByName(name); }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(typename String<T>::Struct*); }
    [[nodiscard]] ordered_json Export(Context const& context) const override;
    void Draw(Context const& context) const override;
};
struct Color : TypeInfo::SymbolType
{
    std::array<byte, 4> Swizzle;

    Color(char const* name, std::array<byte, 4> swizzle) : SymbolType(name), Swizzle(swizzle) { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return context.Data<uint32>(); }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(uint32); }
    [[nodiscard]] ordered_json Export(Context const& context) const override;
    void Draw(Context const& context) const override;
};
template<typename T, size_t N>
struct Point : TypeInfo::SymbolType
{
    Point(char const* name) : SymbolType(name) { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return context.Data<T>(); }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(T) * N; }
    [[nodiscard]] ordered_json Export(Context const& context) const override;
    void Draw(Context const& context) const override;
};
struct GUID : TypeInfo::SymbolType
{
    GUID() : SymbolType("GUID") { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] std::optional<uint32> GetIcon(Context const& context) const override;
    [[nodiscard]] std::optional<ContentObject const*> GetMap(Context const& context) const override;
    [[nodiscard]] bool IsContent() const override { return true; }
    [[nodiscard]] std::optional<ContentObject const*> GetContent(Context const& context) const override;
    [[nodiscard]] bool IsInline() const override { return false; }
    [[nodiscard]] uint32 Size() const override { return sizeof(GW2Viewer::GUID); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return context.Data<GW2Viewer::GUID>(); }
    void Draw(Context const& context) const override;
};
struct Token32 : TypeInfo::SymbolType
{
    Token32() : SymbolType("Token32") { }

    [[nodiscard]] static auto GetDecoded(byte const* data) { return ((GW2Viewer::Token32 const*)data)->GetString(); }
    [[nodiscard]] static auto GetDecoded(Context const& context) { return context.Data<GW2Viewer::Token32>().GetString(); }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return context.Data<uint32>(); }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return GetDecoded(context).data(); }
    [[nodiscard]] uint32 Size() const override { return sizeof(uint32); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return GetDecoded(context).data(); }
    void Draw(Context const& context) const override;
};
struct Token64 : TypeInfo::SymbolType
{
    Token64() : SymbolType("Token64") { }

    [[nodiscard]] static auto GetDecoded(byte const* data) { return ((GW2Viewer::Token64 const*)data)->GetString(); }
    [[nodiscard]] static auto GetDecoded(Context const& context) { return context.Data<GW2Viewer::Token64>().GetString(); }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return context.Data<uint64>(); }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return GetDecoded(context).data(); }
    [[nodiscard]] uint32 Size() const override { return sizeof(uint64); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return GetDecoded(context).data(); }
    void Draw(Context const& context) const override;
};
struct StringID : TypeInfo::SymbolType
{
    StringID() : SymbolType("StringID") { }

    [[nodiscard]] static uint32 GetStringID(Context const& context) { return context.Data<uint32>(); }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return context.Data<uint32>(); }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(uint32); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return GetStringID(context); } // TODO
    void Draw(Context const& context) const override;
};
struct FileID : TypeInfo::SymbolType
{
    FileID() : SymbolType("FileID") { }

    [[nodiscard]] static uint32 GetFileID(Context const& context) { return context.Data<uint32>(); }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return context.Data<uint32>(); }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return std::format("FileID: {}", GetFileID(context)); }
    [[nodiscard]] std::optional<uint32> GetIcon(Context const& context) const override { return GetFileID(context); }
    [[nodiscard]] uint32 Alignment() const override { return sizeof(wchar_t*); }
    [[nodiscard]] uint32 Size() const override { return sizeof(uint32); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return GetFileID(context); } // TODO
    void Draw(Context const& context) const override;
};
struct RawPointerT : TypeInfo::SymbolType
{
    RawPointerT(char const* name = "T*") : SymbolType(name) { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return (TypeInfo::Condition::ValueType)*GetPointer(context); }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return { }; }
    [[nodiscard]] std::optional<byte const*> GetPointer(Context const& context) const override { return context.Data<byte const*>(); }
    [[nodiscard]] uint32 Size() const override { return sizeof(void*); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return ordered_json::object(); }
    void Draw(Context const& context) const override;
};
struct ContentPointer : RawPointerT
{
    ContentPointer() : RawPointerT("Content*") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] std::optional<uint32> GetIcon(Context const& context) const override;
    [[nodiscard]] std::optional<ContentObject const*> GetMap(Context const& context) const override;
    [[nodiscard]] bool IsContent() const override { return true; }
    [[nodiscard]] std::optional<ContentObject const*> GetContent(Context const& context) const override;
    [[nodiscard]] bool IsInline() const override { return false; }
    [[nodiscard]] uint32 Size() const override { return sizeof(ContentObject const*); }
    [[nodiscard]] ordered_json Export(Context const& context) const override;
    void Draw(Context const& context) const override;
};
struct ArrayT : TypeInfo::SymbolType
{
    ArrayT(char const* name = "Array<T>") : SymbolType(name) { }

    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return { }; }
    [[nodiscard]] bool IsArray() const override { return true; }
    [[nodiscard]] std::optional<uint32> GetArrayCount(Context const& context) const override { return *(uint32 const*)&(&context.Data<byte const*>())[1]; }
    [[nodiscard]] std::optional<byte const*> GetPointer(Context const& context) const override { return context.Data<byte const*>(); }
    [[nodiscard]] uint32 Alignment() const override { return sizeof(void*); }
    [[nodiscard]] uint32 Size() const override { return sizeof(void*) + sizeof(uint32); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return ordered_json::array(); }
    void Draw(Context const& context) const override;
};
struct ArrayContent : ArrayT
{
    ArrayContent() : ArrayT("Array<Content>") { }

    [[nodiscard]] bool IsContent() const override { return true; }
    [[nodiscard]] std::optional<ContentObject const*> GetContent(Context const& context) const override;
    [[nodiscard]] uint32 Size() const override { return sizeof(ContentObject const*) + sizeof(uint32); }
    [[nodiscard]] ordered_json Export(Context const& context) const override { return ordered_json::array(); }
    void Draw(Context const& context) const override;
};
struct ParamValue : TypeInfo::SymbolType
{
    ParamValue() : SymbolType("ParamValue") { }

    struct Struct
    {
        uint32 ContentType;
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
    [[nodiscard]] static Struct const& GetStruct(Context const& context) { return context.Data<Struct>(); }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override;
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] std::optional<uint32> GetIcon(Context const& context) const override;
    [[nodiscard]] std::optional<ContentObject const*> GetMap(Context const& context) const override;
    [[nodiscard]] std::optional<byte const*> GetPointer(Context const& context) const override;
    [[nodiscard]] bool IsContent() const override { return true; }
    [[nodiscard]] std::optional<ContentObject const*> GetContent(Context const& context) const override;
    [[nodiscard]] bool IsInline() const override { return false; }
    [[nodiscard]] uint32 Size() const override { return sizeof(Struct); }
    [[nodiscard]] ordered_json Export(Context const& context) const override;
    void Draw(Context const& context) const override;
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
    [[nodiscard]] static Struct const& GetStruct(Context const& context) { return context.Data<Struct>(); }

    [[nodiscard]] std::strong_ordering CompareDataForSearch(byte const* dataA, byte const* dataB) const override;
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override;
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return { }; }
    [[nodiscard]] std::optional<uint32> GetIcon(Context const& context) const override;
    [[nodiscard]] std::optional<ContentObject const*> GetMap(Context const& context) const override;
    [[nodiscard]] std::optional<byte const*> GetPointer(Context const& context) const override;
    [[nodiscard]] bool IsContent() const override { return true; }
    [[nodiscard]] std::optional<ContentObject const*> GetContent(Context const& context) const override;
    [[nodiscard]] bool IsInline() const override { return false; }
    [[nodiscard]] uint32 Alignment() const override { return sizeof(void*); }
    [[nodiscard]] uint32 Size() const override { return sizeof(Struct); }
    [[nodiscard]] ordered_json Export(Context const& context) const override;
    void Draw(Context const& context) const override;
};

struct MetaSymbolType : virtual TypeInfo::SymbolType
{
    MetaSymbolType() : SymbolType(nullptr) { }

    [[nodiscard]] bool IsVisible() const override { return false; }
    [[nodiscard]] std::optional<TypeInfo::Condition::ValueType> GetValueForCondition(Context const& context) const override { return { }; }
    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return { }; }
    [[nodiscard]] uint32 Size() const override { return 0; }
    [[nodiscard]] ordered_json Export(Context const& context) const override { std::terminate(); };
    void Draw(Context const& context) const override { }
};
struct MetaContentName : MetaSymbolType
{
    MetaContentName() : SymbolType("@name") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
};
struct MetaContentPath : MetaSymbolType
{
    MetaContentPath() : SymbolType("@path") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
};
struct MetaContentType : MetaSymbolType
{
    MetaContentType() : SymbolType("@type") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
};
struct MetaContentIcon : MetaSymbolType
{
    MetaContentIcon() : SymbolType("@icon") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] std::optional<uint32> GetIcon(Context const& context) const override;
};
struct MetaContentMap : MetaSymbolType
{
    MetaContentMap() : SymbolType("@map") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
    [[nodiscard]] std::optional<ContentObject const*> GetMap(Context const& context) const override;
};
struct MetaContentDisplay : MetaSymbolType
{
    MetaContentDisplay() : SymbolType("@display") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override;
};
struct MetaContentIconName : MetaContentIcon, MetaContentName
{
    MetaContentIconName() : SymbolType("@iconname") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return std::format("{}{}", MetaContentIcon::GetDisplayText(context), MetaContentName::GetDisplayText(context)); }
};
struct MetaContentIconDisplay : MetaContentIcon, MetaContentDisplay
{
    MetaContentIconDisplay() : SymbolType("@icondisplay") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return std::format("{}{}", MetaContentIcon::GetDisplayText(context), MetaContentDisplay::GetDisplayText(context)); }
};
struct MetaContentSelf : MetaContentName, MetaContentIcon, MetaContentMap
{
    MetaContentSelf() : SymbolType("@self") { }

    [[nodiscard]] std::string GetDisplayText(Context const& context) const override { return MetaContentName::GetDisplayText(context); }
};

std::vector<TypeInfo::SymbolType const*>& GetTypes();

}
