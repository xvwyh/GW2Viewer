export module GW2Viewer.UI.Controls:AsyncProgressBar;
import GW2Viewer.UI.ImGui;
import GW2Viewer.Utils.Async;
#include "Macros.h"

export namespace GW2Viewer::UI::Controls
{

void AsyncProgressBar(Utils::Async::Scheduler const& scheduler)
{
    if (auto context = scheduler.Current())
    {
        I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
        if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
        if (scoped::WithColorVar(ImGuiCol_Border, 0))
        if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
        if (scoped::WithColorVar(ImGuiCol_Text, 0))
        if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
            I::ProgressBar(context.IsIndeterminate() ? -I::GetTime() : context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
    }
}

}
