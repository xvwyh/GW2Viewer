module GW2Viewer.Data.Content;
import :ContentObject;
import :Symbols;
import GW2Viewer.Common;
import GW2Viewer.Data.Game;
import std;

namespace GW2Viewer::Data::Content
{

static std::vector metaContentSymbols
{
    std::from_range,
    Symbols::GetTypes()
    | std::views::filter([](TypeInfo::SymbolType const* type) { return !type->IsVisible() && type->Name[0] == '@'; })
    | std::views::transform([](TypeInfo::SymbolType const* type) -> TypeInfo::Symbol { return { .Type = type->Name }; })
};

static_assert(std::is_trivially_copyable_v<SymbolPath::Part>);
SymbolPath::SymbolPath(std::string_view path)
{
    for (auto const& pathPart : std::views::split(path, std::string_view("->")))
    {
        auto& part = Parts.emplace_back(std::string_view(pathPart));

        auto const& string = part.Value.String;
        if (string.front() == '@')
        {
            if (string.starts_with("@ref"))
                part = string.size() > 6 && string[4] == '<' && string[string.size() - 1] == '>' ? G::Game.Content.GetType(string.substr(5, string.size() - 6)) : nullptr;
            else if (auto const itr = std::ranges::find(metaContentSymbols, string, &TypeInfo::Symbol::Type); itr != metaContentSymbols.end())
                part = *itr;
        }
        else if (string == "..")
            part.Type = Type::Backtrack;
    }
}

template<typename T>
concept SymbolDataSearcher = requires(T const a, TypeInfo::Symbol& symbol, byte const* data, ContentObject const& relative)
{
    { a.Root() } -> std::same_as<SymbolPath::Part>;
    { a.CanSearch() } -> std::same_as<bool>;
    { a.CanCheck(symbol) } -> std::same_as<bool>;
    { a.CanReturn(symbol, data) } -> std::same_as<bool>;
    { a.CanEarlyReturn() } -> std::same_as<bool>;
    { a.CanBacktrack() } -> std::same_as<bool>;
    { a.CanBacktrackFrom(data) } -> std::same_as<bool>;
    { a.CanStepIntoNonInlineContent() } -> std::same_as<bool>;
    { a.Deeper() } -> std::convertible_to<T>;
    { a.Deeper(relative) } -> std::convertible_to<T>;
};
template<SymbolDataSearcher Searcher>
QuerySymbolDataResult::Generator QuerySymbolDataImpl(TypeInfo::LayoutStack& layoutStack, std::span<byte const> fullData, Searcher searcher)
{
    if (!searcher.CanSearch())
        co_return;

    auto const& frame = layoutStack.top();
    auto const& content = *frame.Content;
    switch (SymbolPath::Part const root = searcher.Root(); root.Type)
    {
        case SymbolPath::Type::String:
            break;
        case SymbolPath::Type::Meta:
        {
            co_yield { &content, content, *root.Value.MetaSymbol };
            co_return;
        }
        case SymbolPath::Type::Reference:
        {
            if (frame.DataStart)
                co_return;

            for (auto const& [object, type] : content.IncomingReferences)
            {
                if (!root.Value.ReferenceType || root.Value.ReferenceType == object->Type)
                {
                    object->Finalize();

                    if (auto deeper = searcher.Deeper(content); deeper.CanSearch())
                    {
                        layoutStack.emplace(object, &object->Type->GetTypeInfo().Layout, std::nullopt /* not used here, omitted for performance reasons */, 0);
                        for (auto& result : QuerySymbolDataImpl(layoutStack, object->Data, deeper))
                            co_yield result;
                        layoutStack.pop();
                    }
                    else
                        co_yield { object, *object, metaContentSymbols.back() };
                }
            }
            co_return;
        }
        case SymbolPath::Type::Backtrack:
        {
            if (layoutStack.size() <= 1)
                co_return;

            auto backup = layoutStack.top();
            layoutStack.pop();
            for (auto& result : QuerySymbolDataImpl(layoutStack, fullData, searcher.Deeper()))
                co_yield result;
            layoutStack.push(backup);
            co_return;
        }
    }

    bool const backtrack = searcher.CanBacktrack();
    bool const deeper = searcher.Deeper().CanSearch();
    for (auto& [offset, symbol] : frame.Layout->Symbols)
    {
        if (!searcher.CanCheck(symbol))
            continue;

        if (symbol.Condition && !symbol.Condition->Field.empty() && !symbol.TestCondition(content, layoutStack))
            continue;

        byte const* p = &content.Data[frame.DataStart + offset];
        if (backtrack)
        {
            if (searcher.CanBacktrackFrom(p))
            {
                if (auto deeper = searcher.Deeper().Deeper(); deeper.CanSearch())
                {
                    auto backtrackStack = layoutStack;
                    for (auto& result : QuerySymbolDataImpl(backtrackStack, fullData, deeper))
                        co_yield result;
                }
                else
                    co_yield { p, content, symbol };
            }
            continue;
        }

        if (searcher.CanReturn(symbol, p))
            co_yield { p, content, symbol };

        if (!deeper)
            continue;

        if (auto const traversal = symbol.GetTraversalInfo({ p, content, symbol }, searcher.CanStepIntoNonInlineContent()))
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
        if (auto const itr = std::ranges::find_if(typeInfo.Layout.Symbols, [&](auto& pair) { return searcher.CanCheck(pair.second) && !(pair.second.Condition && !pair.second.Condition->Field.empty() && !pair.second.TestCondition(content, TypeInfo::LayoutStack { { { &content, &typeInfo.Layout } } })); }); itr != typeInfo.Layout.Symbols.end())
            if (auto data = &content.Data[itr->first]; searcher.CanReturn(itr->second, data))
                co_yield { data, content, itr->second };
        co_return;
    }

    TypeInfo::LayoutStack layoutStack;
    layoutStack.emplace(&content, &typeInfo.Layout);
    for (auto& result : QuerySymbolDataImpl(layoutStack, content.Root ? content.Root->Data : content.Data, searcher))
        co_yield result;
}

QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, SymbolPath::Span path)
{
    struct PathSearcher
    {
        SymbolPath::Span Path;
        ContentObject const* Relative = nullptr;
        [[nodiscard]] SymbolPath::Part Root() const { return Path.front(); }
        [[nodiscard]] bool CanSearch() const { return !Path.empty(); }
        [[nodiscard]] bool CanCheck(TypeInfo::Symbol& symbol) const { return symbol.Name == Path.front().Value.String; }
        [[nodiscard]] bool CanReturn(TypeInfo::Symbol& symbol, byte const* data) const { return Path.size() == 1; }
        [[nodiscard]] bool CanEarlyReturn() const { return Path.size() == 1 && Root().Type == SymbolPath::Type::String; }
        [[nodiscard]] bool CanBacktrack() const { return Relative && Path.size() > 1 && Path[1].Type == SymbolPath::Type::Backtrack; }
        [[nodiscard]] bool CanBacktrackFrom(byte const* data) const { return CanBacktrack() && Relative->Data.data() == *(byte const* const*)data; }
        [[nodiscard]] bool CanStepIntoNonInlineContent() const { return true; }
        [[nodiscard]] PathSearcher Deeper() const { return { Path.subspan(1), Relative }; }
        [[nodiscard]] PathSearcher Deeper(ContentObject const& relative) const { return { Path.subspan(1), &relative }; }
    };
    for (auto& result : QuerySymbolDataImpl(content, PathSearcher { path }))
        co_yield result;
}
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, std::string_view path)
{
    for (SymbolPath const symbolPath { path }; auto& result : QuerySymbolData(content, symbolPath))
        co_yield result;
}
QuerySymbolDataResult::Generator QuerySymbolData(ContentObject const& content, TypeInfo::SymbolType const& type, TypeInfo::Condition::ValueType value)
{
    struct TypeSearcher
    {
        ContentObject const& Content;
        TypeInfo::SymbolType const& Type;
        TypeInfo::Condition::ValueType Value;
        [[nodiscard]] SymbolPath::Part Root() const { return { }; }
        [[nodiscard]] bool CanSearch() const { return true; }
        [[nodiscard]] bool CanCheck(TypeInfo::Symbol& symbol) const { return true; }
        [[nodiscard]] bool CanReturn(TypeInfo::Symbol& symbol, byte const* data) const { return symbol.Type == Type.Name && Type.GetValueForCondition({ data, Content, symbol }) == Value; }
        [[nodiscard]] bool CanEarlyReturn() const { return false; }
        [[nodiscard]] bool CanBacktrack() const { return false; }
        [[nodiscard]] bool CanBacktrackFrom(byte const* data) const { return false; }
        [[nodiscard]] bool CanStepIntoNonInlineContent() const { return false; }
        [[nodiscard]] TypeSearcher const& Deeper() const { return *this; }
        [[nodiscard]] TypeSearcher const& Deeper(ContentObject const& relative) const { return Deeper(); }
    };
    for (auto& result : QuerySymbolDataImpl(content, TypeSearcher { content, type, value }))
        co_yield result;
}

}
