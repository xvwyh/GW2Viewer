export module GW2Viewer.Data.Content:Query;
import :TypeInfo;
import <experimental/generator>;

export namespace GW2Viewer::Data::Content
{
struct ContentObject;

struct QuerySymbolDataResult
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
        QuerySymbolDataResult const* operator->() { return &*begin(); }
        template<typename T> operator T() { return **this; }
        operator ContentObject() = delete;
        operator ContentObject() const = delete;
        operator ContentObject const() = delete;
        operator ContentObject const() const = delete;
        operator ContentObject* () { return *begin(); }
        operator ContentObject const* () { return *begin(); }
        operator ContentObject& () { return *begin(); }
        operator ContentObject const& () { return (ContentObject const&)*begin(); }
    };
    TypeInfo::Symbol* Symbol = nullptr;
    byte const* Data = nullptr;

    [[nodiscard]] auto GetContent() const { return Symbol->GetType()->GetContent(Data); }
    template<typename T> [[nodiscard]] decltype(auto) As() const { return *(T const*)Data; }
    template<> [[nodiscard]] decltype(auto) As<ContentObject*>() const { return GetContent().value_or(nullptr); }
    template<> [[nodiscard]] decltype(auto) As<ContentObject const*>() const { return As<ContentObject*>(); }
    template<> [[nodiscard]] decltype(auto) As<ContentObject>() const { return *As<ContentObject*>(); }
    template<> [[nodiscard]] decltype(auto) As<ContentObject const>() const { return *As<ContentObject const*>(); }
    template<typename T> operator T() const { return As<T>(); }
    operator ContentObject() = delete;
    operator ContentObject() const = delete;
    operator ContentObject const() = delete;
    operator ContentObject const() const = delete;
    operator ContentObject* () const { return As<ContentObject*>(); }
    operator ContentObject const* () const { return As<ContentObject const*>(); }
    operator ContentObject& () const { return As<ContentObject>(); }
    operator ContentObject const& () const { return As<ContentObject const>(); }
};
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject& content, std::span<std::string_view> path);
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject& content, std::string_view path);
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject& content, TypeInfo::SymbolType const& type, TypeInfo::Condition::ValueType value);

}
