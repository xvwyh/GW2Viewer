module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Controls:FilteredComboBox;

export namespace GW2Viewer::UI::Controls
{

template<typename T>
struct FilteredComboBoxOptions
{
    float MaxHeight = (I::GetFontSize() + I::GetStyle().ItemSpacing.y) * 8 - I::GetStyle().ItemSpacing.y + I::GetStyle().FramePadding.x * 2;
    std::function<std::string(T const& value)> Formatter = [](T const& value) { return std::format("{}", value); };
    std::function<bool(T const& value, ImGuiTextFilter const& filter, FilteredComboBoxOptions const& options)> Filter = [](T const& value, ImGuiTextFilter const& filter, FilteredComboBoxOptions const& options)
    {
        return filter.PassFilter(options.Formatter(value).c_str());
    };
    std::function<bool(T const& value, bool selected, FilteredComboBoxOptions const& options)> Draw = [](T const& value, bool selected, FilteredComboBoxOptions const& options)
    {
        return I::Selectable(options.Formatter(value).c_str(), selected);
    };
};

template<typename T, typename Range>
bool FilteredComboBox(char const* label, T& currentValue, Range&& values, FilteredComboBoxOptions<T> options = { })
{
    bool result = false;

    if (scoped::WithStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(I::GetStyle().FramePadding.x, I::GetStyle().FramePadding.x)))
    if (scoped::Combo(label, options.Formatter(currentValue).c_str(), ImGuiComboFlags_HeightLargest))
    {
        static ImGuiTextFilter filter;
        if (I::IsWindowAppearing())
        {
            I::SetKeyboardFocusHere();
            filter.Clear();
        }
        I::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F);
        filter.Draw("##Filter", -FLT_MIN);

        I::SetNextWindowSizeConstraints({ 0, 0 }, { FLT_MAX, options.MaxHeight });
        if (scoped::Child("##Items", { -FLT_MIN, 0 }, ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY))
        {
            for (auto const& value : values)
            {
                if (options.Filter(value, filter, options))
                {
                    if (options.Draw(value, currentValue == value, options))
                    {
                        result = true;
                        currentValue = value;
                        I::CloseCurrentPopup();
                    }
                }
            }
        }
    }

    return result;
}

}
