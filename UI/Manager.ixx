export module GW2Viewer.UI.Manager;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.Time;
import GW2Viewer.Content.Event;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Content;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Viewers.Viewer;
import GW2Viewer.Utils.Async.ProgressBarContext;
import std;

export namespace GW2Viewer::UI
{

class Manager
{
public:
    struct
    {
        ImFont* Default { };
        ImFont* GameText { };
        ImFont* GameTextItalic { };
        ImFont* GameHeading { };
        ImFont* GameHeadingItalic { };
    } Fonts;

    bool IsLoaded() const { return m_loaded; }
    void Load();
    void Unload();

    void Update();

    void OpenWorldMap(bool newTab = false);

    std::string MakeDataLink(byte type, uint32 id);

    void PlayVoice(uint32 voiceID);

    void ExportData(std::span<byte const> data, std::filesystem::path const& path);

    void Defer(std::function<void()>&& func) { m_deferred.emplace_back(std::move(func)); }
    auto GetTime() const { return m_now; }
    auto DeltaTime() const { return m_deltaTime; }

    template<typename T> requires std::is_base_of_v<Viewers::Viewer, T>
    auto GetCurrentViewer() const { return dynamic_cast<T*>(GetCurrentViewer()); }
    auto GetCurrentViewer() const { return m_currentViewer; }

    auto GetNewViewerID() { return m_nextViewerID++; }
    void AddViewer(std::unique_ptr<Viewers::Viewer>&& viewer) { m_viewers.emplace_back(std::move(viewer)); }
    template<typename T> requires std::is_base_of_v<Viewers::Viewer, T>
    T& ReplaceViewer(T& oldViewer, std::unique_ptr<T>&& newViewer)
    {
        if (auto itr = std::ranges::find(m_viewers, &oldViewer, [](auto const& ptr) { return ptr.get(); }); itr != m_viewers.end())
        {
            *itr = std::move(newViewer);

            if (m_currentViewer == &oldViewer)
                m_currentViewer = itr->get();

            return *(T*)itr->get();
        }
        return oldViewer;
    }

private:
    bool m_loaded = false;

    std::list<std::function<void()>> m_deferred;

    float m_deltaTime = 1.0f;
    Time::PrecisePoint m_now;

    bool m_showOriginalNames = false;

    std::array<Utils::Async::ProgressBarContext, 10> m_progress;

    std::list<std::unique_ptr<Viewers::Viewer>> m_listViewers;
    std::list<std::unique_ptr<Viewers::Viewer>> m_viewers;
    Viewers::Viewer* m_currentViewer = nullptr;
    uint32 m_nextViewerID = 0;
};

}

export namespace GW2Viewer::G { UI::Manager UI; }
