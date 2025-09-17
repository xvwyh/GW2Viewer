export module GW2Viewer.UI.Controls:ContentNamespaceButton;
import GW2Viewer.Data.Content;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Manager;
import GW2Viewer.Utils.Encoding;
import std;
#include "Macros.h"

namespace GW2Viewer::UI::Controls
{
void OpenContentNamespace(Data::Content::ContentNamespace const& ns);

export
{

struct ContentNamespaceButtonOptions
{
};

void ContentNamespaceButton(Data::Content::ContentNamespace const* ns, void const* id, ContentNamespaceButtonOptions const& options = { })
{
    scoped::WithID(id);

    if (G::UI.Hovered.Namespace.Is(ns))
        I::PushStyleColor(ImGuiCol_Button, I::GetColorU32(ImGuiCol_ButtonHovered));
    I::Button(std::format(ICON_FA_FOLDER " {}", Utils::Encoding::ToUTF8(ns ? ns->GetFullDisplayName() : L"???")).c_str());
    if (G::UI.Hovered.Namespace.Is(ns))
        I::PopStyleColor();
    G::UI.Hovered.Namespace.SetLastItem(ns);

    if (ns)
        if (I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
            OpenContentNamespace(*ns);
}

}

}
