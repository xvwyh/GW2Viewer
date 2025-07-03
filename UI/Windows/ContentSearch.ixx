module;
#include "UI/ImGui/ImGui.h"
#include "Utils/Async.h"

export module GW2Viewer.UI.Windows.ContentSearch;
import GW2Viewer.Common;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Exception;
import std;

export namespace GW2Viewer::UI::Windows
{

struct ContentSearch : Window
{
    Utils::Async::Scheduler Async;
    std::mutex Lock;
    Data::Content::TypeInfo::SymbolType const* Symbol;
    Data::Content::TypeInfo::Condition::ValueType Value;
    std::vector<Data::Content::ContentObject*> Results;

    void SearchForSymbolValue(std::string_view symbolTypeName, Data::Content::TypeInfo::Condition::ValueType value)
    {
        Show();
        Async.Run([this, symbolTypeName, value](Utils::Async::Context context)
        {
            {
                std::scoped_lock _(Lock);
                Symbol = Data::Content::Symbols::GetByName(symbolTypeName);
                Value = value;
                Results.clear();
            }
            auto _ = Utils::Exception::SEHandler::Create();
            context->SetTotal(G::Game.Content.GetObjects().size());
            std::for_each(std::execution::par_unseq, G::Game.Content.GetObjects().begin(), G::Game.Content.GetObjects().end(), [this, context, processed = 0](Data::Content::ContentObject* content) mutable
            {
                CHECK_SHARED_ASYNC;
                try
                {
                    content->Finalize();
                    if (auto generator = QuerySymbolData(*content, *Symbol, Value); generator.begin() != generator.end())
                    {
                        std::scoped_lock _(Lock);
                        Results.emplace_back(content);
                    }
                }
                catch (...) { }
                if (static constexpr uint32 interval = 1000; !(++processed % interval))
                    context->InterlockedIncrement(interval);
            });
            context->Finish();
        });
    }

    std::string Title() override { return "Content Search"; }
    void Draw() override
    {
        std::scoped_lock __(Lock);
        I::SetNextItemWidth(-FLT_MIN);
        if (scoped::Disabled(true))
            I::InputText("##Description", (char*)std::format("Content that contains {} fields with value: {}", Symbol->Name, Value).c_str(), 9999);
        if (auto context = Async.Current())
        {
            I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
            if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
            if (scoped::WithColorVar(ImGuiCol_Border, 0))
            if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
            if (scoped::WithColorVar(ImGuiCol_Text, 0))
            if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                if (context.IsIndeterminate())
                    I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                else
                    I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
        }
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
        if (scoped::Child("Content", { }, 0, ImGuiWindowFlags_AlwaysVerticalScrollbar))
            for (auto const& object : Results)
                Controls::ContentButton(object, &object);
    }
};

}

export namespace GW2Viewer::G::Windows { UI::Windows::ContentSearch ContentSearch; }
