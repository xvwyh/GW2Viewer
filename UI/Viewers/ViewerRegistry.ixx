export module GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.Common;
import GW2Viewer.UI.Viewers.Viewer;
import std;

export namespace GW2Viewer::UI::Viewers
{

enum class Category
{
    ListViewer,
    ObjectViewer,
    Uncategorized,
};

struct ViewerRegistry
{
    struct Info
    {
        char const* Title;
        char const* Name;
        Category Category;
    };

    using ConstructorFunction = std::unique_ptr<Viewer>(*)(uint32 id, bool newTab);

    struct RegisteredViewer
    {
        Info Info;
        ConstructorFunction Constructor;
    };

    static auto& GetRegistry()
    {
        static std::list<RegisteredViewer> instance { };
        return instance;
    }

    static RegisteredViewer const* GetByName(std::string_view name)
    {
        auto itr = std::ranges::find(GetRegistry(), name, [](RegisteredViewer const& registered) { return registered.Info.Name; });
        return itr != GetRegistry().end() ? &*itr : nullptr;
    }

    template<typename Viewer>
    static Info const& Register(Info const& info)
    {
        auto& registered = GetRegistry().emplace_back(info, nullptr);
        if constexpr (std::is_constructible_v<Viewer, uint32, bool>)
            registered.Constructor = [](uint32 id, bool newTab) -> std::unique_ptr<Viewers::Viewer> { return std::make_unique<Viewer>(id, newTab); };
        return registered.Info;
    }
};

template<typename Viewer, ViewerRegistry::Info Info>
struct RegisterViewer
{
    inline static ViewerRegistry::Info const& ViewerInfo = ViewerRegistry::Register<Viewer>(Info);
};

}
