export module GW2Viewer.UI.Viewers.FileViewers;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Data.Archive;
import GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.UI.Viewers.PackFileViewer;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct FileViewers
{
    static auto& GetRegistry()
    {
        static std::unordered_map<fcc, std::function<FileViewer*(uint32 id, bool newTab, Data::Archive::File const& file)>> instance { };
        return instance;
    }

    template<fcc FourCC>
    struct For;
    template<fcc FourCC>
    struct For : FileViewer { };

    template<fcc FourCC>
    class Register
    {
        static bool DoRegister()
        {
            return [] { return GetRegistry().emplace(FourCC, []<typename... Args>(Args&&... args) { return new For<FourCC>(std::forward<Args>(args)...); }).second; }();
        }
        inline static bool m_registered = DoRegister();
    };
};

template<> struct FileViewers::For<fcc::PF5> : Register<fcc::PF5>, PackFileViewer { using PackFileViewer::PackFileViewer; };
template<> struct FileViewers::For<fcc::PF4> : Register<fcc::PF4>, PackFileViewer { using PackFileViewer::PackFileViewer; };
template<> struct FileViewers::For<fcc::PF3> : Register<fcc::PF3>, PackFileViewer { using PackFileViewer::PackFileViewer; };
template<> struct FileViewers::For<fcc::PF2> : Register<fcc::PF2>, PackFileViewer { using PackFileViewer::PackFileViewer; };
template<> struct FileViewers::For<fcc::PF1> : Register<fcc::PF1>, PackFileViewer { using PackFileViewer::PackFileViewer; };

}
