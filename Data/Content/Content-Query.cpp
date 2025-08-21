module GW2Viewer.Data.Content;
import :ContentObject;
import GW2Viewer.Common;
import GW2Viewer.Data.Game;
import std;

namespace GW2Viewer::Data::Content
{

template<typename T>
concept SymbolDataSearcher = requires(T const a, TypeInfo::Symbol const& symbol, byte const* data)
{
    { a.CanCheck(symbol) } -> std::same_as<bool>;
    { a.CanReturn(symbol, data) } -> std::same_as<bool>;
    { a.CanEarlyReturn() } -> std::same_as<bool>;
    { a.CanStepIntoNonInlineContent() } -> std::same_as<bool>;
    { a.Deeper() } -> std::convertible_to<T>;
};
template<SymbolDataSearcher Searcher>
QuerySymbolDataResult::Generator QuerySymbolDataImpl(TypeInfo::LayoutStack& layoutStack, std::span<byte const> fullData, Searcher searcher)
{
    auto const& frame = layoutStack.top();
    auto const& content = *frame.Content;
    for (auto& [offset, symbol] : frame.Layout->Symbols)
    {
        if (!searcher.CanCheck(symbol))
            continue;

        if (symbol.Condition && !symbol.Condition->Field.empty() && !symbol.TestCondition(content, layoutStack))
            continue;

        byte const* p = &content.Data[frame.DataStart + offset];
        if (searcher.CanReturn(symbol, p))
            co_yield { &symbol, p };

        if (auto const traversal = symbol.GetTraversalInfo(p, searcher.CanStepIntoNonInlineContent()))
        {
            // Shitty solution, shouldn't normally happen if layouts are perfectly defined, but that's an impossible dream
            if (traversal.Type->IsInline())
                if (*traversal.Start < fullData.data() || *traversal.Start >= fullData.data() + fullData.size())
                    continue;

            auto const elements = std::span(*traversal.Start, traversal.Type->IsInline() ? (size_t)fullData.data() + fullData.size() : std::dynamic_extent) | std::views::stride(traversal.Size) | std::views::take(traversal.ArrayCount.value_or(1)) | std::views::enumerate;
            for (auto [index, element] : elements)
            {
                auto const target = &element;
                if (!traversal.Type->IsContent())
                {
                    layoutStack.emplace(&content, &symbol.GetElementLayout(), std::nullopt /* not used here, omitted for performance reasons */, (uint32)std::distance(frame.Content->Data.data(), target));
                    for (auto& result : QuerySymbolDataImpl(layoutStack, fullData, searcher.Deeper()))
                        co_yield result;
                    layoutStack.pop();
                }
                else if (auto const content = G::Game.Content.GetByDataPointer(target))
                {
                    content->Finalize();
                    layoutStack.emplace(content, &content->Type->GetTypeInfo().Layout, std::nullopt /* not used here, omitted for performance reasons */, 0);
                    for (auto& result : QuerySymbolDataImpl(layoutStack, traversal.Type->IsInline() ? fullData : content->Data, searcher.Deeper()))
                        co_yield result;
                    layoutStack.pop();
                }
            }
        }
    }
}
template<SymbolDataSearcher Searcher>
QuerySymbolDataResult::Generator QuerySymbolDataImpl(ContentObject const& content, Searcher searcher)
{
    auto& typeInfo = content.Type->GetTypeInfo();
    if (searcher.CanEarlyReturn())
    {
        // Cheap and fast version if no deep traversal is needed
        if (auto const itr = std::ranges::find_if(typeInfo.Layout.Symbols, [&](auto const& pair) { return searcher.CanCheck(pair.second) && !(pair.second.Condition && !pair.second.Condition->Field.empty() && !pair.second.TestCondition(content, TypeInfo::LayoutStack { { { &content, &typeInfo.Layout } } })); }); itr != typeInfo.Layout.Symbols.end())
            if (auto data = &content.Data[itr->first]; searcher.CanReturn(itr->second, data))
                co_yield { &itr->second, data };
        co_return;
    }

    TypeInfo::LayoutStack layoutStack;
    layoutStack.emplace(&content, &typeInfo.Layout);
    for (auto& result : QuerySymbolDataImpl(layoutStack, content.Root ? content.Root->Data : content.Data, searcher))
        co_yield result;
}
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, std::span<std::string_view> path)
{
    struct PathSearcher
    {
        std::span<std::string_view> Path;
        [[nodiscard]] bool CanCheck(TypeInfo::Symbol const& symbol) const { return symbol.Name == Path.front(); }
        [[nodiscard]] bool CanReturn(TypeInfo::Symbol const& symbol, byte const* data) const { return Path.size() == 1; }
        [[nodiscard]] bool CanEarlyReturn() const { return Path.size() == 1; }
        [[nodiscard]] bool CanStepIntoNonInlineContent() const { return true; }
        [[nodiscard]] PathSearcher Deeper() const { return { Path.subspan(1) }; }
    };
    for (auto& result : QuerySymbolDataImpl(content, PathSearcher { path }))
        co_yield result;
}
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, std::string_view path)
{
    std::vector<std::string_view> parts;
    for (auto const& part : std::views::split(path, std::string_view("->")))
        parts.emplace_back(part);

    for (auto& result : QuerySymbolData(content, parts))
        co_yield result;
}
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, TypeInfo::SymbolType const& type, TypeInfo::Condition::ValueType value)
{
    struct TypeSearcher
    {
        TypeInfo::SymbolType const& Type;
        TypeInfo::Condition::ValueType Value;
        [[nodiscard]] bool CanCheck(TypeInfo::Symbol const& symbol) const { return true; }
        [[nodiscard]] bool CanReturn(TypeInfo::Symbol const& symbol, byte const* data) const { return symbol.Type == Type.Name && Type.GetValueForCondition(data) == Value; }
        [[nodiscard]] bool CanEarlyReturn() const { return false; }
        [[nodiscard]] bool CanStepIntoNonInlineContent() const { return false; }
        [[nodiscard]] TypeSearcher const& Deeper() const { return *this; }
    };
    for (auto& result : QuerySymbolDataImpl(content, TypeSearcher { type, value }))
        co_yield result;
}

}
