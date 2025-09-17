export module GW2Viewer.UI.Controls:ContentButton;
import :Texture;
import GW2Viewer.Data.Content;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.Viewer;
import GW2Viewer.Utils.Encoding;
import std;
#include "Macros.h"

namespace GW2Viewer::UI::Controls
{
void OpenContent(Data::Content::ContentObject const& content, Viewers::OpenViewerOptions const& options);

export
{

struct ContentButtonOptions
{
    std::string_view Icon = ICON_FA_ARROW_RIGHT;
    std::string_view MissingTypeName = "???";
    std::string_view MissingContentName;
    struct CondenseContext
    {
        bool FullName = false;
        bool TypeName = false;
        bool Condense() { return !std::exchange(TypeName, std::exchange(FullName, true)); }
    };
    CondenseContext* SharedCondenseContext = nullptr;
};

void ContentButton(Data::Content::ContentObject const* content, void const* id, ContentButtonOptions const& options = { })
{
    scoped::WithID(id);

    ContentButtonOptions::CondenseContext localCondense;
    auto& condense = options.SharedCondenseContext ? *options.SharedCondenseContext : localCondense;

    std::string textPreIcon, textPostIcon;
    ImVec2 sizePreIcon, sizePostIcon, size;
    auto const icon = content ? content->GetIcon() : 0;
    auto const iconSize = icon ? ImVec2(I::GetFrameHeight(), I::GetFrameHeight()) : ImVec2();
    auto const padding = I::GetStyle().FramePadding;
    do
    {
        textPreIcon = std::vformat(condense.TypeName ? "{} " : "{} <c=#4>{}</c> ", std::make_format_args(options.Icon, content ? Utils::Encoding::ToUTF8(content->Type->GetDisplayName()) : options.MissingTypeName));
        textPostIcon = std::format("{}", content ? Utils::Encoding::ToUTF8(condense.FullName ? content->GetDisplayName() : content->GetFullDisplayName()) : options.MissingContentName);
        sizePreIcon = I::CalcTextSize(textPreIcon.c_str(), textPreIcon.c_str() + textPreIcon.size());
        sizePostIcon = I::CalcTextSize(textPostIcon.c_str(), textPostIcon.c_str() + textPostIcon.size());
        size = padding * 2;
        size.x += sizePreIcon.x + iconSize.x + sizePostIcon.x;
        size.y += std::max(sizePreIcon.y, sizePostIcon.y);
    }
    while (size.x > I::GetContentRegionAvail().x && condense.Condense());

    auto const pos = I::GetCursorScreenPos();
    if (G::UI.Hovered.Object.Is(content))
        I::PushStyleColor(ImGuiCol_Button, I::GetColorU32(ImGuiCol_ButtonHovered));
    I::Button("", size);
    if (G::UI.Hovered.Object.Is(content))
        I::PopStyleColor();
    G::UI.Hovered.Object.SetLastItem(content);

    if (content)
        if (auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
            OpenContent(*content, { .MouseButton = button });

    ImRect bb(pos, pos + size);
    I::RenderTextClipped(bb.Min + padding, bb.Max - padding, textPreIcon.c_str(), textPreIcon.c_str() + textPreIcon.size(), &sizePreIcon, { }, &bb);
    bb.Min.x += sizePreIcon.x;
    if (icon && !content->Type->GetTypeInfo().DisplayFormat.contains("@icon"))
        if (scoped::WithCursorScreenPos(bb.Min + ImVec2(padding.x, 0)))
            if (Texture(icon, { .Size = iconSize, .AdvanceCursor = false }))
                bb.Min.x += iconSize.x;
    I::RenderTextClipped(bb.Min + padding, bb.Max - padding, textPostIcon.c_str(), textPostIcon.c_str() + textPostIcon.size(), &sizePostIcon, { }, &bb);
}

}

}
