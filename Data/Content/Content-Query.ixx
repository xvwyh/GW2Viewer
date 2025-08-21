export module GW2Viewer.Data.Content:Query;
import :TypeInfo;
import <experimental/generator>;

export namespace GW2Viewer::Data::Content
{
struct ContentObject;

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
        QuerySymbolDataResult const* operator->() { return &*begin(); }
        template<typename T> operator T() { return **this; }
        operator ContentObject() = delete;
        operator ContentObject() const = delete;
        operator ContentObject const() = delete;
        operator ContentObject const() const = delete;
        operator ContentObject const* () { return *begin(); }
        operator ContentObject const& () { return (ContentObject const&)*begin(); }
    };

    using Context::Context;

    [[nodiscard]] auto GetContent() const { return Symbol.GetType()->GetContent(*this); }
    template<typename T> operator T() const { return Data<T>(); }
    operator ContentObject() = delete;
    operator ContentObject() const = delete;
    operator ContentObject const() = delete;
    operator ContentObject const() const = delete;
    operator ContentObject const* () const { return Data<ContentObject const*>(); }
    operator ContentObject const& () const { return Data<ContentObject const>(); }
};
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, std::span<std::string_view> path);
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, std::string_view path);
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, TypeInfo::SymbolType const& type, TypeInfo::Condition::ValueType value);

}
