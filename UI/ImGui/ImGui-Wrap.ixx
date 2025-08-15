module;
#include <imgui_internal.h>
#include "dep/imguiwrap.dear.h"

export module GW2Viewer.UI.ImGui:Wrap;
import :Core;

export namespace dear
{

using Window = Begin;
using dear::Child;
using dear::Group;
using dear::Combo;
using dear::ListBox;
using dear::MenuBar;
using dear::MainMenuBar;
using dear::Menu;
using dear::Table;
using dear::Tooltip;
using dear::CollapsingHeader;
using dear::TreeNode;
using dear::SeparatedTreeNode;
using dear::TreeNodeEx;
using dear::SeparatedTreeNodeEx;
using dear::Popup;
using dear::PopupModal;
using dear::TabBar;
using dear::TabItem;
using dear::WithStyleVar;
using dear::ItemTooltip;
using dear::WithID;
using dear::OverrideID;
using dear::Disabled;
using dear::Indent;

struct WithCursorPos : ScopeWrapper<WithCursorPos>
{
    WithCursorPos(ImVec2 const& pos) noexcept : ScopeWrapper(true) { ImGui::SetCursorPos(pos); }
    WithCursorPos(float x, float y) noexcept : WithCursorPos(ImVec2(x, y)) { }
    void dtor() noexcept { ImGui::SetCursorPos(restore); ImGui::GetCurrentWindow()->DC.IsSetPos = false; }
    ImVec2 const restore = ImGui::GetCursorPos();
};
struct WithCursorScreenPos : ScopeWrapper<WithCursorScreenPos>
{
    WithCursorScreenPos(ImVec2 const& pos) noexcept : ScopeWrapper(true) { ImGui::SetCursorScreenPos(pos); }
    WithCursorScreenPos(float x, float y) noexcept : WithCursorScreenPos(ImVec2(x, y)) { }
    void dtor() noexcept { ImGui::SetCursorScreenPos(restore); ImGui::GetCurrentWindow()->DC.IsSetPos = false; }
    ImVec2 const restore = ImGui::GetCursorScreenPos();
};
struct WithCursorOffset : ScopeWrapper<WithCursorOffset>
{
    WithCursorOffset(ImVec2 const& offset) noexcept : ScopeWrapper(true) { ImGui::SetCursorScreenPos(restore + offset); }
    WithCursorOffset(float x, float y) noexcept : WithCursorOffset(ImVec2(x, y)) { }
    void dtor() noexcept { ImGui::SetCursorScreenPos(restore); ImGui::GetCurrentWindow()->DC.IsSetPos = false; }
    ImVec2 const restore = ImGui::GetCursorScreenPos();
};
struct WithColorVar : ScopeWrapper<WithColorVar>
{
    WithColorVar(ImGuiCol idx, ImVec4 const& col) noexcept : ScopeWrapper(true) { ImGui::PushStyleColor(idx, col); }
    WithColorVar(ImGuiStyleVar idx, ImU32 col) noexcept : ScopeWrapper(true) { ImGui::PushStyleColor(idx, col); }
    static void dtor() noexcept { ImGui::PopStyleColor(); }
};
struct Font : ScopeWrapper<Font>
{
    Font(ImFont* font, float size) noexcept : ScopeWrapper(true) { ImGui::PushFont(font, size); }
    static void dtor() noexcept { ImGui::PopFont(); }
};
struct PopupContextItem : ScopeWrapper<PopupContextItem>
{
    PopupContextItem(const char* str_id = NULL, ImGuiPopupFlags popup_flags = 1) noexcept : ScopeWrapper(ImGui::BeginPopupContextItem(str_id, popup_flags)) { }
    static void dtor() noexcept { ImGui::EndPopup(); }
};
struct TableBackgroundChannel : ScopeWrapper<TableBackgroundChannel>
{
    TableBackgroundChannel() noexcept : ScopeWrapper(true) { ImGui::TablePushBackgroundChannel(); }
    static void dtor() noexcept { ImGui::TablePopBackgroundChannel(); }
};

}
