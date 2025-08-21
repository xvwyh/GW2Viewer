module GW2Viewer.UI.Viewers.ContentViewer;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.ContentListViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Windows.ListContentValues;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Exception;
import GW2Viewer.Utils.Sort;
#include "Macros.h"

namespace GW2Viewer::UI::Viewers
{

std::string ContentViewer::Title()
{
    return Utils::Encoding::ToUTF8(std::format(L"<c=#4>{}</c> {}", Content.Type->GetDisplayName(), Content.GetDisplayName()));
}

void ContentViewer::Draw()
{
    auto _ = Utils::Exception::SEHandler::Create();

    auto& typeInfo = G::Config.TypeInfo.try_emplace(Content.Type->Index).first->second;
    typeInfo.Initialize(*Content.Type);

    auto tabScopeID = I::GetCurrentWindow()->IDStack.back();
    if (scoped::Child(I::GetSharedScopeID("ContentViewer"), { }, ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
    {
        DrawHistoryButtons();

        I::SameLine();
        if (auto const* guid = Content.GetGUID())
            if (auto bookmarked = std::ranges::contains(G::Config.BookmarkedContentObjects, *guid, &decltype(G::Config.BookmarkedContentObjects)::value_type::Value); I::CheckboxButton(ICON_FA_BOOKMARK, bookmarked, "Bookmark", I::GetFrameHeight()))
                if (bookmarked)
                    G::Config.BookmarkedContentObjects.emplace(Time::Now(), *guid);
                else
                    std::erase_if(G::Config.BookmarkedContentObjects, [guid = *guid](auto const& record) { return record.Value == guid; });

        I::SameLine();
        if (I::Button(std::format("<c=#{}>{}</c> <c=#{}>{}</c>", G::Config.TreeContentStructLayout ? "4" : "F", ICON_FA_LIST, G::Config.TreeContentStructLayout ? "F" : "4", ICON_FA_FOLDER_TREE).c_str()))
            G::Config.TreeContentStructLayout ^= true;

        I::SameLine();
        if (scoped::TabBar("Tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_NoTabListScrollingButtons))
        {
            if (scoped::TabItem(ICON_FA_INFO " Info", nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
            if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
            if (scoped::OverrideID(tabScopeID)) // Inherit tab's ID scope, to ensure that keyboard focus is cleared when tabs change
            {
                auto const dataID = Content.GetDataID();
                I::SetNextItemWidth(100);
                I::InputTextReadOnly("##FullMangledName", Utils::Encoding::ToUTF8(Content.GetFullName()).c_str());
                I::SameLine();
                I::SetNextItemWidth(dataID ? -90 : -FLT_MIN);
                I::InputTextReadOnly("##FullName", Utils::Encoding::ToUTF8(Content.GetFullDisplayName()));
                if (dataID)
                {
                    I::SameLine();
                    I::SetNextItemWidth(20);
                    I::DragScalar("##DataLinkType", ImGuiDataType_U8, &typeInfo.DataLinkType, 0.1f, nullptr, nullptr, "%02hhX");
                    I::SameLine();
                    I::SetNextItemWidth(-FLT_MIN);
                    if (scoped::Disabled(!typeInfo.DataLinkType))
                    if (I::Button(ICON_FA_COPY " DataLink"))
                        I::SetClipboardText(G::UI.MakeDataLink(typeInfo.DataLinkType, *dataID).c_str());
                }
                if (scoped::Group())
                {
                    if (I::InputTextUTF8("Content Name", G::Config.ContentObjectNames, *Content.GetGUID(), Content.GetName() && Content.GetName()->Name && *Content.GetName()->Name ? *Content.GetName()->Name : Content.GetDisplayName()))
                        G::Viewers::Notify(&ContentListViewer::ClearCache);
                    I::InputTextUTF8("Namespace Name", G::Config.ContentNamespaceNames, Content.Namespace->GetFullName(), Content.Namespace->Name);
                    I::InputTextWithHint("Type Name", Utils::Encoding::ToUTF8(Content.Type->GetDisplayName()).c_str(), &typeInfo.Name);
                }
                I::SameLine();
                I::AlignTextToFramePadding();
                I::TextUnformatted(ICON_FA_PEN_TO_SQUARE);
                if (scoped::ItemTooltip())
                    I::TextUnformatted("Type Notes");
                I::SameLine();
                if (scoped::Group())
                    I::InputTextMultiline("##TypeNotes", &typeInfo.Notes, { -1, -1 }, ImGuiInputTextFlags_AllowTabInput);
            }

            auto const referenceSorter = [](Data::Content::ContentObject::Reference const& ref) { return std::make_tuple(ref.Type, ref.Object->GetFullDisplayName(), ref.Object->GetFullName(), ref.Object->Type->Index, ref.Object->Index); };

            if (!Content.OutgoingReferences.empty())
                I::PushStyleColor(ImGuiCol_Text, 0xFF00FF00);
            if (scoped::TabItem(std::format(ICON_FA_ARROW_RIGHT " Outgoing References ({})###OutgoingReferences", Content.OutgoingReferences.size()).c_str(), nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton | (Content.OutgoingReferences.empty() ? 0 : ImGuiTabItemFlags_UnsavedDocument)))
            {
                using enum Data::Content::ContentObject::Reference::Types;
                if (!Content.OutgoingReferences.empty())
                    I::PopStyleColor();
                I::SetNextWindowSizeConstraints({ }, { FLT_MAX, 300 });
                if (scoped::Child("Scroll", { }, ImGuiChildFlags_AutoResizeY))
                if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
                for (auto const& [object, type] : Utils::Sort::ComplexSorted(Content.OutgoingReferences, false, referenceSorter))
                    Controls::ContentButton(object, object, { .Icon = type == Root ? ICON_FA_ARROW_TURN_DOWN_RIGHT : type == Tracked ? ICON_FA_CHEVRONS_RIGHT : ICON_FA_ARROW_RIGHT });
            }
            else if (!Content.OutgoingReferences.empty())
                I::PopStyleColor();

            if (!Content.IncomingReferences.empty())
                I::PushStyleColor(ImGuiCol_Text, 0xFF00FF00);
            if (scoped::TabItem(std::format(ICON_FA_ARROW_LEFT " Incoming References ({})###IncomingReferences", Content.IncomingReferences.size()).c_str(), nullptr, ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton | (Content.IncomingReferences.empty() ? 0 : ImGuiTabItemFlags_UnsavedDocument)))
            {
                using enum Data::Content::ContentObject::Reference::Types;
                if (!Content.IncomingReferences.empty())
                    I::PopStyleColor();
                I::SetNextWindowSizeConstraints({ }, { FLT_MAX, 300 });
                if (scoped::Child("Scroll", { }, ImGuiChildFlags_AutoResizeY))
                if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
                for (auto const& [object, type] : Utils::Sort::ComplexSorted(Content.IncomingReferences, false, referenceSorter))
                {
                    Controls::ContentButton(object, object, { .Icon = type == Root ? ICON_FA_ARROW_TURN_LEFT_UP : type == Tracked ? ICON_FA_CHEVRONS_LEFT : ICON_FA_ARROW_LEFT });
                    if (object->Root)
                        if (scoped::Indent(20))
                            Controls::ContentButton(object->Root, object->Root, { .Icon = ICON_FA_ARROW_TURN_LEFT_UP });
                }
            }
            else if (!Content.IncomingReferences.empty())
                I::PopStyleColor();
        }
    }

    using DrawType = Data::Content::TypeInfo::Symbol::DrawType;
    ImGuiID tableLayoutScope = I::GetCurrentWindow()->GetID("TableLayoutScope");
    ImGuiID popupDefineStructLayoutSymbol = I::GetCurrentWindow()->GetID("DefineStructLayoutSymbol");
    ImGuiID popupChangeStructLayoutSymbol = I::GetCurrentWindow()->GetID("ChangeStructLayoutSymbol");

    struct Pointer
    {
        Data::Content::TypeInfo::Symbol* Symbol;
        uint32 Size;
        uint32 ArraySize;
        uint32 Index;
        std::string ParentPath;
    };
    using Pointers = std::map<byte const*, std::list<Pointer>>;
    struct RenderContentEditorContext
    {
        DrawType Draw;
        std::span<byte const> FullData;
        std::span<byte const> ScopedData;
        uint32 ScopeOffset;
        Data::Content::TypeInfo::StructLayout* Layout;
        Data::Content::ContentObject const* Content;
        Data::Content::TypeInfo::LayoutStack TreeLayoutStack;
        std::vector<Pointers*> PointersStack;
        std::vector<std::tuple<std::string, uint32>> OutTableColumns;
    };
    auto renderContentEditor = [this, &typeInfo, tableLayoutScope, popupChangeStructLayoutSymbol, popupDefineStructLayoutSymbol](RenderContentEditorContext& context, auto& renderContentEditor) -> void
    {
        auto const& data = context.ScopedData;

        constexpr int INDENT = 30;
        Data::Content::TypeInfo::LayoutStack layoutStack;
        layoutStack.emplace(context.Content, context.Layout, context.TreeLayoutStack.top().Path);

        Pointers pointers;
        ImVec2 highlightPointerCursor;
        uint32 i = 0;
        uint32 unmappedStart = 0;
        auto flushHexEditor = [&]
        {
            if (i > unmappedStart)
            {
                auto const& frame = layoutStack.top();
                if (frame.IsFolded)
                    return;

                bool const empty = std::ranges::none_of(data.subspan(unmappedStart, i - unmappedStart), std::identity());
                switch (context.Draw)
                {
                    case DrawType::TableCountColumns:
                        context.OutTableColumns.emplace_back("Unmapped", 180);
                        return;
                    case DrawType::TableHeader:
                        I::TableNextColumn();
                        if (scoped::Disabled(true))
                            I::ButtonEx("<c=#444>" ICON_FA_GEAR "</c>");
                        I::SameLine(0, 0);
                        I::AlignTextToFramePadding();
                        I::SetCursorPosY(I::GetCursorPosY() + I::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
                        if (scoped::WithColorVar(ImGuiCol_Text, { 1, 1, 1, 0.25f }))
                            I::TableHeader(empty ? std::format("###{}", I::TableGetColumnName()).c_str() : I::TableGetColumnName());
                        return;
                    case DrawType::TableRow:
                        I::TableNextColumn();
                        if (empty)
                            return I::TextUnformatted("<c=#444>" ICON_FA_0 "</c>");
                        [[fallthrough]];
                    case DrawType::Inline:
                        break;
                }

                Controls::HexViewerOptions options
                {
                    .StartOffset = unmappedStart + context.ScopeOffset,
                    .ShowHeaderRow = false,
                    .ShowOffsetColumn = context.Draw != DrawType::TableRow,
                    .ShowVerticalScroll = context.Draw != DrawType::TableRow,
                    .OutHighlightOffset = highlightOffset,
                    .OutHighlightPointer = highlightPointer,
                    .ByteMap = context.Content ? context.Content->ByteMap : nullptr,
                };
                HexViewer(data.subspan(unmappedStart, i - unmappedStart), options);
                if (auto const offset = options.OutHighlightOffset)
                    highlightOffset = offset;
                else if (options.OutHoveredInfo)
                    highlightOffset.reset();
                if (auto const pointer = options.OutHighlightPointer)
                    highlightPointer = pointer;
                else if (options.OutHoveredInfo)
                    highlightPointer.reset();
                if (options.OutHoveredInfo && I::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    persistentHovered = options.OutHoveredInfo;
                    creatingSymbol = { G::Config.TreeContentStructLayout ? context.TreeLayoutStack : layoutStack, *layoutStack.top().Path, frame.DataStart + context.ScopeOffset, false, false };
                    I::OpenPopup(popupDefineStructLayoutSymbol);
                }
            }
        };

        bool lastFrameWasFolded = false;
        while (i <= data.size())
        {
            auto* p = data.data() + i;
            if (auto itr = pointers.find(p); itr != pointers.end())
            {
                for (auto const& pointer : itr->second)
                {
                    //if (!G::Config.TreeContentStructLayout)
                    if (!(layoutStack.size() > 1 && G::Config.TreeContentStructLayout))
                        flushHexEditor();
                    
                    auto const symbolType = pointer.Symbol->GetType();
                    bool const isContentPointer = symbolType->IsContent();
                    auto* layout = [&]() -> Data::Content::TypeInfo::StructLayout*
                    {
                        if (isContentPointer)
                        {
                            if (auto const content = G::Game.Content.GetByDataPointer(data.data() + (pointer.Size ? i : layoutStack.top().ObjectStart)))
                            {
                                auto& elementTypeInfo = G::Config.TypeInfo.try_emplace(content->Type->Index).first->second;
                                elementTypeInfo.Initialize(*content->Type);
                                return &elementTypeInfo.Layout;
                            }

                            return nullptr;
                        }

                        return &pointer.Symbol->GetElementLayout();
                    }();
                    if (pointer.Size)
                    {
                        if (!pointer.ArraySize || !pointer.Index)
                        {
                            if (G::Config.TreeContentStructLayout)
                            {
                                I::TextColored({ 1, 1, 1, 0.1f }, ICON_FA_EYE_SLASH " %u bytes omitted: " ICON_FA_FOLDER_TREE " %s", pointer.Size * std::max(1u, pointer.ArraySize), pointer.Symbol->GetFullPath(pointer.ParentPath).c_str());
                            }
                            else
                            {
                                scoped::WithID(context.ScopeOffset + i);
                                if (!lastFrameWasFolded)
                                    I::Dummy({ 1, 10 });
                                if (highlightPointer == p)
                                    highlightPointerCursor = I::GetCursorScreenPos();
                                if (I::Button(pointer.Symbol->Folded ? ICON_FA_CHEVRON_RIGHT "##Fold" : ICON_FA_CHEVRON_DOWN "##Fold"))
                                    pointer.Symbol->Folded ^= true;
                                I::SameLine(0, 0);
                                I::Text("%s->", pointer.Symbol->GetFullPath(pointer.ParentPath).c_str());
                                I::SameLine();
                                if (!pointer.Symbol->Autogenerated/* && context.Draw != DrawType::TableRow ???*/)
                                {
                                    bool const edit = I::Button(std::format("<c=#{}>" ICON_FA_GEAR "</c>##EditFromListTarget", pointer.Symbol->Condition.has_value() ? "FF0" : std::ranges::contains(typeInfo.NameFields, pointer.Symbol->GetFullPath(pointer.ParentPath)) ? "FEA" : std::ranges::contains(typeInfo.IconFields, pointer.Symbol->GetFullPath(pointer.ParentPath)) ? "EAF" : "FFF").c_str());
                                    I::SameLine(0, 0);

                                    if (edit)
                                    {
                                        editingSymbol = { G::Config.TreeContentStructLayout ? context.TreeLayoutStack : layoutStack, pointer.ParentPath, pointer.Symbol, I::GetCurrentContext()->LastItemData.Rect.GetBL(), I::GetCurrentContext()->LastItemData.Rect.GetTL(), false };
                                        I::OpenPopup(popupChangeStructLayoutSymbol);
                                    }
                                }
                                I::SetNextItemWidth(120);
                                I::DragCoerceInt("##ElementSizeFromListTarget", (int*)&pointer.Symbol->ElementSize, 0.1f, 1, 10000, "Size: %u bytes", ImGuiSliderFlags_AlwaysClamp, [](auto v) { return std::max(1, (v / 4) * 4); });
                                I::Indent(INDENT);
                            }
                            auto const& top = layoutStack.top();
                            layoutStack.emplace(
                                context.Content,
                                layout,
                                pointer.Symbol->GetFullPath(pointer.ParentPath),
                                i,
                                isContentPointer ? i : top.ObjectStart,
                                isContentPointer ? layoutStack.size() : top.ObjectStackDepth,
                                pointer.Symbol->Folded);
                        }
                        if (pointer.ArraySize)
                        {
                            if (auto& frame = layoutStack.top(); frame.Layout == layout)
                            {
                                frame.DataStart = i;
                                if (!frame.IsFolded && !G::Config.TreeContentStructLayout)
                                {
                                    I::Unindent(INDENT);
                                    I::TextColored({ 1, 1, 1, 0.25f }, "[<c=#FFF>%u</c>]", pointer.Index);
                                    I::SameLine();
                                    I::Indent(INDENT);
                                }
                            }
                        }
                    }
                    else
                    {
                        if (auto const& frame = layoutStack.top(); !pointer.ArraySize || frame.Layout == layout)
                        {
                            if (!G::Config.TreeContentStructLayout)
                            {
                                I::Unindent(INDENT);
                                if (highlightPointer == data.data() + frame.DataStart)
                                    I::GetWindowDrawList()->AddLine(highlightPointerCursor, I::GetCursorScreenPos(), 0xFF0000FF, 2);
                                lastFrameWasFolded = frame.IsFolded;
                                if (!lastFrameWasFolded)
                                    I::Dummy({ 1, 10 });
                            }
                            layoutStack.pop();
                        }
                    }
                    unmappedStart = i;
                }
            }
            if (layoutStack.empty())
                break;
            auto const& frame = layoutStack.top();
            if (!frame.Layout)
            {
                ++i;
                continue;
            }
            bool render = !(layoutStack.size() > 1 && G::Config.TreeContentStructLayout);

            auto& layout = frame.Layout->Symbols;
            auto typeLocalOffset = i - frame.DataStart;
            if (auto itr = layout.find(typeLocalOffset); itr != layout.end() || i == data.size())
            {
                if (itr != layout.end())
                {
                    itr = layout.end();
                    auto [candidateFrom, candidateTo] = layout.equal_range(typeLocalOffset);
                    for (auto candidateItr = candidateFrom; candidateItr != candidateTo && itr == layout.end(); ++candidateItr)
                    {
                        auto const& symbol = candidateItr->second;
                        if (symbol.Condition && !symbol.Condition->Field.empty())
                        {
                            if (context.Content && candidateItr->second.TestCondition(*context.Content, G::Config.TreeContentStructLayout && context.TreeLayoutStack.size() > 1 ? context.TreeLayoutStack : layoutStack))
                                itr = candidateItr;
                        }
                        else
                            itr = candidateItr;
                    }

                    if (itr == layout.end())
                    {
                        ++i;
                        continue;
                    }
                }

                if (render)
                    flushHexEditor();

                if (itr != layout.end())
                {
                    auto& symbol = itr->second;

                    if (highlightOffset && *highlightOffset == i)
                        I::GetForegroundDrawList()->AddLine(I::GetCursorScreenPos(), I::GetCursorScreenPos() + ImVec2(0, 16), 0xFFFF0000, 2);
                    if (highlightPointer && *highlightPointer == p)
                        I::GetForegroundDrawList()->AddLine(I::GetCursorScreenPos(), I::GetCursorScreenPos() + ImVec2(0, 16), 0xFF0000FF, 2);

                    auto cursor = I::GetCursorScreenPos();
                    if (render && !frame.IsFolded)
                    {
                        switch (context.Draw)
                        {
                            case DrawType::TableCountColumns:
                                context.OutTableColumns.emplace_back(symbol.Name, 0);
                                render = false;
                                break;
                            case DrawType::TableHeader:
                                render = false;
                            case DrawType::TableRow:
                                I::TableNextColumn();
                                [[fallthrough]];
                            case DrawType::Inline:
                                if (!symbol.Autogenerated && context.Draw != DrawType::TableRow)
                                {
                                    scoped::WithID(context.ScopeOffset + i);
                                    bool const edit = I::Button(std::format("<c=#{}>" ICON_FA_GEAR "</c>##Edit", symbol.Condition.has_value() ? "FF0" : std::ranges::contains(typeInfo.NameFields, symbol.GetFullPath(*frame.Path)) ? "FEA" : std::ranges::contains(typeInfo.IconFields, symbol.GetFullPath(*frame.Path)) ? "EAF" : "FFF").c_str());
                                    I::SameLine(0, 0);

                                    if (edit)
                                    {
                                        editingSymbol = { G::Config.TreeContentStructLayout ? context.TreeLayoutStack : layoutStack, *frame.Path, &symbol, I::GetCurrentContext()->LastItemData.Rect.GetBL(), I::GetCurrentContext()->LastItemData.Rect.GetTL(), false };
                                        I::OpenPopup(popupChangeStructLayoutSymbol);
                                    }
                                }
                                symbol.Draw(p, context.Draw, *context.Content);
                                break;
                        }
                    }

                    uint32 const symbolSize = symbol.Size();
                    uint32 const symbolAlignedSize = symbol.AlignedSize();

                    if (render && i + symbolSize > data.size())
                        I::GetWindowDrawList()->AddRectFilled(cursor, I::GetCurrentContext()->LastItemData.Rect.GetBR(), 0x400000FF);

                    if (render && symbolSize != symbolAlignedSize && std::ranges::any_of(p + symbolSize, p + symbolAlignedSize, std::identity()))
                        I::GetWindowDrawList()->AddRectFilled(cursor, I::GetCurrentContext()->LastItemData.Rect.GetBR(), 0x400080FF);

                    unmappedStart = i += symbolAlignedSize;

                    if (auto const traversal = symbol.GetTraversalInfo(p))
                    {
                        if (render && I::IsMouseHoveringRect(cursor, I::GetCurrentContext()->LastItemData.Rect.GetBR()))
                            highlightPointer = *traversal.Start;

                        auto const elements = std::span(*traversal.Start, context.FullData.data() + context.FullData.size()) | std::views::stride(traversal.Size) | std::views::take(traversal.ArrayCount.value_or(1)) | std::views::enumerate;

                        auto markPointers = [&elements, &symbol, &traversal, &frame](auto& pointers)
                        {
                            for (auto [index, element] : elements)
                                pointers[&element].emplace_back(&symbol, traversal.Size, traversal.ArrayCount.value_or(0), index, *frame.Path);
                            pointers[*traversal.Start + traversal.Size * traversal.ArrayCount.value_or(1)].emplace_front(&symbol, 0, traversal.ArrayCount.value_or(0), -1, *frame.Path);
                        };
                        markPointers(pointers);
                        for (auto parentPointers : context.PointersStack)
                            markPointers(*parentPointers);

                        if (render && G::Config.TreeContentStructLayout && traversal.Type->IsInline())
                        {
                            scoped::WithID(context.ScopeOffset + i);
                            if (I::Button(symbol.Folded ? ICON_FA_CHEVRON_RIGHT "##Fold" : ICON_FA_CHEVRON_DOWN "##Fold"))
                                symbol.Folded ^= true;
                            if (symbol.Folded)
                            {
                                I::SameLine();
                                I::TextColored({ 1, 1, 1, 0.25f }, "%u bytes folded", traversal.Size * traversal.ArrayCount.value_or(1));
                            }
                            else
                            {
                                auto const rect = I::GetCurrentContext()->LastItemData.Rect;
                                I::SameLine(-1, 1);
                                bool table = false;
                                for (auto [index, element] : elements)
                                {
                                    auto const target = &element;
                                    if (scoped::Indent(rect.GetWidth()))
                                    {
                                        if (traversal.ArrayCount && !symbol.Table)
                                        {
                                            I::TextColored({ 1, 1, 1, 0.25f }, "[<c=#FFF>%u</c>]", index);
                                            auto const text = I::GetCurrentContext()->LastItemData.Rect;
                                            if (index)
                                                I::GetWindowDrawList()->AddLine(
                                                    { (float)(int)((rect.Min.x + rect.Max.x) / 2) + 1, (float)(int)((text.Min.y + text.Max.y) / 2) },
                                                    { (float)(int)rect.Max.x, (float)(int)((text.Min.y + text.Max.y) / 2) },
                                                    0x40FFFFFF);
                                            I::SameLine();
                                            I::Indent(INDENT);
                                        }
                                        do
                                        {
                                            RenderContentEditorContext options
                                            {
                                                .Draw = DrawType::Inline,
                                                .FullData = context.FullData,
                                                .ScopedData = { target, traversal.Size },
                                                .ScopeOffset = context.ScopeOffset + (uint32)std::distance(data.data(), target),
                                                .Layout = &symbol.GetElementLayout(),
                                                .Content = context.Content,
                                                .TreeLayoutStack = context.TreeLayoutStack,
                                                .PointersStack = context.PointersStack,
                                            };
                                            if (traversal.Type->IsContent())
                                            {
                                                if (auto const content = G::Game.Content.GetByDataPointer(&element))
                                                {
                                                    content->Finalize();
                                                    auto& elementTypeInfo = G::Config.TypeInfo.try_emplace(content->Type->Index).first->second;
                                                    elementTypeInfo.Initialize(*content->Type);

                                                    options.ScopedData = /*options.FullData = - deliberately not modified*/ { content->Data.data(), traversal.Size };
                                                    options.ScopeOffset = 0;
                                                    options.Layout = &elementTypeInfo.Layout;
                                                    options.Content = content;
                                                }
                                                else
                                                    break;
                                            }
                                            // WTB: span intersection
                                            if (!(options.ScopedData.data() >= options.FullData.data() && options.ScopedData.data() < options.FullData.data() + options.FullData.size()) ||
                                                !(options.ScopedData.data() + options.ScopedData.size() >= options.FullData.data() && options.ScopedData.data() + options.ScopedData.size() <= options.FullData.data() + options.FullData.size()))
                                            {
                                                I::TextColored({ 1, 0, 0, 1 }, "OUT OF BOUNDS");
                                                break;
                                            }
                                            options.TreeLayoutStack.emplace(options.Content, options.Layout, symbol.GetFullPath(*options.TreeLayoutStack.top().Path), options.ScopeOffset);
                                            options.PointersStack.emplace_back(&pointers);
                                            if (symbol.Table)
                                            {
                                                if (!index)
                                                {
                                                    options.Draw = DrawType::TableCountColumns;
                                                    renderContentEditor(options, renderContentEditor);

                                                    //if (scoped::OverrideID(tableLayoutScope)) // Make all symbols of the same type share table settings
                                                    //if (scoped::WithID(&symbol))
                                                    {
                                                        I::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2());
                                                        I::BeginTableEx("SymbolTable", (ImGuiID)&symbol, options.OutTableColumns.size(), ImGuiTableFlags_Borders | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoSavedSettings);
                                                        table = true;
                                                        for (size_t column = 0; auto const& [name, width] : options.OutTableColumns)
                                                            I::TableSetupColumn(std::format("{}##{}", name, column++).c_str(), ImGuiTableColumnFlags_WidthFixed, width);
                                                    }
                                                    I::TableNextRow(ImGuiTableRowFlags_Headers);
                                                    options.Draw = DrawType::TableHeader;
                                                    renderContentEditor(options, renderContentEditor);
                                                }
                                                I::TableNextRow();
                                                options.Draw = DrawType::TableRow;
                                            }
                                            renderContentEditor(options, renderContentEditor);
                                        }
                                        while (false);
                                        if (traversal.ArrayCount && !symbol.Table)
                                            I::Unindent(INDENT);
                                    }
                                }
                                if (table)
                                //if (scoped::OverrideID(tableLayoutScope)) // Make all symbols of the same type share table settings
                                //if (scoped::WithID(&symbol))
                                {
                                    I::EndTable();
                                    I::PopStyleVar();
                                }

                                I::GetWindowDrawList()->AddLine(
                                    { (float)(int)((rect.Min.x + rect.Max.x) / 2), (float)(int)rect.Max.y },
                                    { (float)(int)((rect.Min.x + rect.Max.x) / 2), (float)(int)I::GetCursorScreenPos().y },
                                    0x40FFFFFF);
                            }
                        }
                    }
                }
                else
                    ++i;
            }
            else
                ++i;
        }
    };

    if (scoped::Child("Scroll", { }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, { I::GetStyle().ItemSpacing.x, 0 }))
    {
        RenderContentEditorContext options
        {
            .Draw = DrawType::Inline,
            .FullData = Content.Data,
            .ScopedData = Content.Data,
            .ScopeOffset = 0,
            .Layout = &typeInfo.Layout,
            .Content = &Content,
        };
        options.TreeLayoutStack.emplace(options.Content, options.Layout, "", options.ScopeOffset);
        renderContentEditor(options, renderContentEditor);
    }

    if (creatingSymbol && persistentHovered)
    {
        auto& [layoutStack, parentPath, typeStartOffset, initialized, up] = *creatingSymbol;
        static Data::Content::TypeInfo::Symbol symbol;
        if (symbol.Type.empty())
            symbol.Type = "uint32";

        auto const& [byteOffset, cellCursor, cellSize, tableCursor, tableSize] = *persistentHovered;
        auto offset = byteOffset - typeStartOffset;
        I::GetWindowDrawList()->AddRectFilled(ImVec2(tableCursor.x, cellCursor.y + 2), ImVec2(tableCursor.x + tableSize.x, cellCursor.y + cellSize.y - 2), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.2f }));
        I::GetWindowDrawList()->AddRectFilled(ImVec2(cellCursor.x + 2, tableCursor.y), ImVec2(cellCursor.x + cellSize.x - 2, tableCursor.y + tableSize.y), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.2f }));
        I::GetWindowDrawList()->AddRect(cellCursor, cellCursor + ImVec2(cellSize.x * symbol.Size(), cellSize.y), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.5f }));
        I::GetWindowDrawList()->AddRectFilled(cellCursor + ImVec2(cellSize.x * symbol.Size(), 0), cellCursor + ImVec2(cellSize.x * symbol.AlignedSize(), cellSize.y), I::ColorConvertFloat4ToU32({ 1, 1, 1, 0.5f }));

        I::SetNextWindowPos(cellCursor + ImVec2(0, up ? 0 : cellSize.y), ImGuiCond_None, { 0, up ? 1.0f : 0.0f });
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, { I::GetStyle().ItemSpacing.x, 0 }))
        if (scoped::Popup("DefineStructLayoutSymbol", ImGuiWindowFlags_NoDecoration))
        {
            auto& layout = layoutStack.top().Layout->Symbols;
            if (!initialized)
            {
                initialized = true;
                static bool symbolWasCopyingUnion = false;

                if (auto itr = std::ranges::find_if(layout, [offset](auto const& pair) { return pair.first == offset; }); itr != layout.end())
                {
                    symbolWasCopyingUnion = true;
                    auto const& existing = itr->second;
                    symbol.Condition = existing.Condition;
                    symbol.Name = existing.Name;
                    symbol.Type = existing.Type;
                    symbol.Enum = existing.Enum;
                    symbol.Alignment = existing.Alignment;
                    if (symbol.Condition && !symbol.Condition->Field.empty())
                    {
                        if (auto const value = symbol.GetValueForCondition(Content, layoutStack))
                            symbol.Condition->Value = *value;
                        else
                            symbol.Condition->Value = { };
                    }
                }
                else if (symbolWasCopyingUnion)
                {
                    symbolWasCopyingUnion = false;
                    symbol.Condition.reset();
                    symbol.Name.clear();
                    symbol.Enum.reset();
                    symbol.Alignment = { };
                }
            }

            I::Text("Define symbol at offset 0x%X (%d)", offset, offset);
            auto const placeholderName = std::format("field{:X}", offset);
            symbol.DrawOptions(typeInfo, layoutStack, { }, true, placeholderName);

            bool close = false;
            if (I::Button(ICON_FA_PLUS " Define"))
            {
                auto& added = layout.emplace(offset, symbol)->second;
                //added.Parent = layoutStack.top().Layout->Parent;
                if (added.Name.empty())
                    added.Name = placeholderName;
                //added.FinishLoading();

                symbol.Name.clear();
                symbol.Condition.reset();

                close = true;
            }

            if (I::SameLine(); I::Button("List All Used Values"))
            {
                auto itr = layout.emplace(offset, symbol);
                auto& added = itr->second;
                //added.Parent = layoutStack.top().Layout->Parent;
                if (added.Name.empty())
                    added.Name = placeholderName;
                //added.FinishLoading();

                G::Windows::ListContentValues.Set(*Content.Type, added, layoutStack);

                layout.erase(itr);
            }

            if (I::GetWindowPos().y + I::GetWindowSize().y > I::GetIO().DisplaySize.y)
                up = true;
            if (I::SameLine(); I::Button("Cancel") || close)
            {
                persistentHovered.reset();
                I::CloseCurrentPopup();
            }
        }
        else
            persistentHovered.reset();
    }
    if (editingSymbol)
    {
        auto& [layoutStack, parentPath, symbol, cursorBL, cursorTL, up] = *editingSymbol;
        auto& layout = layoutStack.top().Layout->Symbols;
        auto symbolItr = std::ranges::find_if(layout, [symbol](auto const& pair) { return &pair.second == symbol; });
        I::SetNextWindowPos(up ? cursorTL : cursorBL, ImGuiCond_None, { 0, up ? 1.0f : 0.0f });
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, { I::GetStyle().ItemSpacing.x, 0 }))
        if (scoped::Popup("ChangeStructLayoutSymbol", ImGuiWindowFlags_NoDecoration))
        {
            I::Text("Editing symbol at offset 0x%X (%d)", symbolItr->first, symbolItr->first);
            I::TextUnformatted(symbol->GetFullPath(parentPath).c_str());
            symbol->DrawOptions(typeInfo, layoutStack, parentPath, false, symbolItr != layout.end() ? std::format("field{:X}", symbolItr->first) : "");
            bool close = I::Button("Close");

            if (I::SameLine(); I::Button("List All Used Values"))
                G::Windows::ListContentValues.Set(*Content.Type, *symbol, layoutStack);

            if (symbolItr != layout.end())
            if (scoped::WithColorVar(ImGuiCol_Text, 0xFF0000FF))
            if (I::SameLine(); I::Button(ICON_FA_XMARK " Undefine"))
            {
                layout.erase(symbolItr);
                close = true;
            }

            if (I::GetWindowPos().y + I::GetWindowSize().y > I::GetIO().DisplaySize.y)
                up = true;
            if (close)
            {
                editingSymbol.reset();
                I::CloseCurrentPopup();
            }
        }
        else
            editingSymbol.reset();
    }
}

}
