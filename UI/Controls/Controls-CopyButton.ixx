module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Controls:CopyButton;
import GW2Viewer.Utils.Format;

export namespace GW2Viewer::UI::Controls
{

void CopyButton(char const* name, std::string const& data, std::string const& preview, bool condition = true)
{
    if (scoped::Disabled(!condition))
    if (I::Button(std::vformat(condition ? ICON_FA_COPY " <c=#8>{}:</c> {}" : ICON_FA_COPY " <c=#8>{}</c>", std::make_format_args(name, I::StripMarkup(preview))).c_str()))
        I::SetClipboardText(data.c_str());
}
void CopyButton(char const* name, std::string const& data, bool condition = true) { CopyButton(name, data, data, condition); }
void CopyButton(char const* name, std::wstring_view data, bool condition = true) { CopyButton(name, Utils::Encoding::ToUTF8(data), data.size() <= 20 ? Utils::Encoding::ToUTF8(data) : Utils::Encoding::ToUTF8(std::format(L"{}...", std::wstring_view(data).substr(0, 20))), condition); }
void CopyButton(char const* name, std::wstring const& data, bool condition = true) { CopyButton(name, std::wstring_view(data), condition); }
void CopyButton(char const* name, wchar_t const* data, bool condition = true) { CopyButton(name, std::wstring_view(data), condition); }
template<typename T>
void CopyButton(char const* name, T const& data, bool condition = true) { CopyButton(name, std::format("{}", data), condition); }

}
