module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Windows.Window;

export namespace GW2Viewer::UI::Windows { struct Window; }

export namespace GW2Viewer::G::Windows
{

auto& GetAllWindows() { static std::list<UI::Windows::Window*> instance; return instance; }

template<typename T>
auto GetWindows() { return GetAllWindows() | std::views::filter([](UI::Windows::Window* window) { return dynamic_cast<T*>(window); }); }

template<typename T, typename Func> requires std::is_base_of_v<UI::Windows::Window, T>
void ForEach(Func&& func) { std::ranges::for_each(GetWindows<T>(), [&func](T* viewer) { return std::invoke(func, *viewer); }); }

template<typename T, typename Result, typename... Args> requires std::is_base_of_v<UI::Windows::Window, T>
void Notify(Result(T::* method)(Args...), Args&&... args) { ForEach<T>(std::bind_back(method, std::forward<Args>(args)...)); }

}

export namespace GW2Viewer::UI::Windows
{

struct Window
{
    Window()
    {
        G::Windows::GetAllWindows().emplace_back(this);
    }
    virtual ~Window()
    {
        G::Windows::GetAllWindows().remove(this);
    }

    void Show() { m_shown = true; }
    void Hide() { m_shown = false; }
    auto& GetShown() { return m_shown; }

    void Update()
    {
        if (!m_shown)
            return;

        if (scoped::Window(Title().c_str(), &m_shown, ImGuiWindowFlags_NoFocusOnAppearing))
            Draw();
    }
    virtual std::string Title() = 0;
    virtual void Draw() = 0;

private:
    bool m_shown = false;
};

}
