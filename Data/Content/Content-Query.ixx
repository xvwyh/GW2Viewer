export module GW2Viewer.Data.Content:Query;
import :TypeInfo;
import <boost/container/small_vector.hpp>;
import <experimental/generator>;

export namespace GW2Viewer::Data::Content
{
struct ContentObject;

struct SymbolPath
{
    enum class Type : byte
    {
        String = 0, // Reusing the last byte of std::string_view for this, which should always be 0 too
        Meta,
        Reference,
        Backtrack,
    };
    union Value
    {
        std::string_view String;
        TypeInfo::Symbol* MetaSymbol;
        ContentTypeInfo const* ReferenceType;
    };
    struct Part
    {
        union
        {
            Value Value;
            struct
            {
                byte Padding[sizeof(Value) - sizeof(std::underlying_type_t<Type>)];
                Type Type;
            };
        };

        Part() : Value({ .String = { } }) { Type = Type::String; }
        Part(std::string_view string) : Value({ .String = string }) { Type = Type::String; }
        Part(TypeInfo::Symbol& metaSymbol) : Value({ .MetaSymbol = &metaSymbol }) { Type = Type::Meta; }
        Part(ContentTypeInfo const* referenceType) : Value({ .ReferenceType = referenceType }) { Type = Type::Reference; }
    };
    using Span = std::span<Part const>;

    boost::container::small_vector<Part, 5> Parts;

    SymbolPath(std::string_view path);

    operator Span() const { return Parts; }
};

struct QuerySymbolDataResult : TypeInfo::Context
{
    //using Generator = std::experimental::generator<QuerySymbolDataResult>;
    struct Generator
    {
        using internal_type = std::experimental::generator<QuerySymbolDataResult>;
        using promise_type = internal_type::promise_type;
        internal_type Internal;
        Generator(internal_type&& internal) : Internal(std::move(internal)) { }
        decltype(auto) begin() { return Internal.begin(); }
        decltype(auto) end() { return Internal.end(); }
        QuerySymbolDataResult const& operator*() { return *begin(); }
        QuerySymbolDataResult const* operator->() { return &**this; }
        template<typename T> operator T() { return **this; }
        operator ContentObject() = delete;
        operator ContentObject() const = delete;
        operator ContentObject const() = delete;
        operator ContentObject const() const = delete;
        operator ContentObject const* () { return **this; }
        operator ContentObject const& () { return **this; }
    };

    using Context::Context;

    template<typename T> operator T() const { return Data<T>(); }
    operator ContentObject() = delete;
    operator ContentObject() const = delete;
    operator ContentObject const() = delete;
    operator ContentObject const() const = delete;
    operator ContentObject const* () const { return Symbol.GetType()->GetContent(*this).value_or(nullptr); }
    operator ContentObject const& () const { return **this; }
};
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, SymbolPath::Span path);
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, std::string_view path);
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, TypeInfo::SymbolType const& type, TypeInfo::Condition::ValueType value);
struct ExportOptions
{
    uint32 InlineObjectMaxDepth = 0;
};
ordered_json ExportSymbolData(ContentObject const& content, ExportOptions const& options = { });

}
