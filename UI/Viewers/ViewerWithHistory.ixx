export module GW2Viewer.UI.Viewers.ViewerWithHistory;
import GW2Viewer.Common;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.Viewer;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import std;
#include "Macros.h"

template<typename T> struct ViewerHistoryType { using Type = T; };
template<typename T> requires std::is_reference_v<T> struct ViewerHistoryType<T> { using Type = std::reference_wrapper<std::remove_reference_t<T>>; };// std::add_pointer_t<std::remove_reference_t<T>>; };

export namespace GW2Viewer::UI::Viewers
{

template<typename Self, typename Target, ViewerRegistry::Info Info>
struct ViewerWithHistory : Viewer, RegisterViewer<Self, Info>
{
    using Base = ViewerWithHistory;
    using ViewerType = Self;
    using TargetType = Target;
    using HistoryType = typename ViewerHistoryType<TargetType>::Type;

    std::stack<HistoryType> HistoryPrev;
    std::stack<HistoryType> HistoryNext;

    using Viewer::Viewer;

    std::string Title() override { return this->ViewerInfo.Title; }

    virtual TargetType GetCurrent() const = 0;
    virtual bool IsCurrent(TargetType target) const = 0;
    static void Open(TargetType target, OpenViewerOptions const& options = { })
    {
        G::UI.Defer([target = HistoryType(target), options]
        {
            if (auto* viewer = G::UI.GetCurrentViewer<ViewerType>(); viewer && !options.OpenInNewTab)
            {
                if (viewer->IsCurrent(target))
                    return;

                auto const id = viewer->ID;
                auto historyPrev = std::move(viewer->HistoryPrev);
                auto historyNext = std::move(viewer->HistoryNext);
                if (!options.HistoryMove)
                {
                    historyPrev.emplace(viewer->GetCurrent());
                    historyNext = { };
                }
                ViewerType::Recreate(viewer, target, options);
                viewer->HistoryPrev = std::move(historyPrev);
                viewer->HistoryNext = std::move(historyNext);
            }
            else
                G::UI.AddViewer(ViewerType::Create(target, options));
        });
    }

    virtual void DrawHistoryButtons()
    {
        if (scoped::Disabled(HistoryPrev.empty()); I::Button(ICON_FA_ARROW_LEFT "##HistoryBack") || I::IsEnabled() && I::GetIO().MouseClicked[3])
        {
            auto content = HistoryPrev.top();
            HistoryPrev.pop();
            HistoryNext.emplace(GetCurrent());
            ViewerType::Open(content, { .HistoryMove = true });
        }
        I::SameLine(0, 0);
        if (scoped::Disabled(HistoryNext.empty()); I::Button(ICON_FA_ARROW_RIGHT "##HistoryNext") || I::IsEnabled() && I::GetIO().MouseClicked[4])
        {
            auto content = HistoryNext.top();
            HistoryNext.pop();
            HistoryPrev.emplace(GetCurrent());
            ViewerType::Open(content, { .HistoryMove = true });
        }
    }

protected:
    static std::unique_ptr<ViewerType> Create(HistoryType target, OpenViewerOptions const& options)
    {
        return std::make_unique<ViewerType>(G::UI.GetNewViewerID(), options.OpenInNewTab, target);
    }
    static void Recreate(ViewerType*& viewer, HistoryType target, OpenViewerOptions const& options)
    {
        auto id = viewer->ID;
        viewer->~ViewerType();
        new(viewer) ViewerType(id, options.OpenInNewTab, target);
    }
};

}
