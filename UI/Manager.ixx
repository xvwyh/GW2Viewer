module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Manager;
import GW2Viewer.Common.GUID;
import GW2Viewer.Content.Event;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Content;
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

    void Load();

    void Update();

    void OpenFile(Data::Archive::File const& file, bool newTab = false, bool historyMove = false);
    void OpenContent(Data::Content::ContentObject& object, bool newTab = false, bool historyMove = false);
    void OpenConversation(uint32 conversationID, bool newTab = false, bool historyMove = false);
    void OpenEvent(Content::EventID eventID, bool newTab = false, bool historyMove = false);
    void OpenWorldMap(bool newTab = false);

    std::string MakeDataLink(byte type, uint32 id);

    void PlayVoice(uint32 voiceID);

    void ExportData(std::span<byte const> data, std::filesystem::path const& path);

    void Defer(std::function<void()>&& func) { m_deferred.emplace_back(std::move(func)); }
    auto GetTime() const { return m_now; }
    auto DeltaTime() const { return m_deltaTime; }

    auto GetCurrentViewer() const { return m_currentViewer; }

private:
    std::list<std::function<void()>> m_deferred;

    float m_deltaTime = 1.0f;
    std::chrono::high_resolution_clock::time_point m_now;

    bool m_showOriginalNames = false;

    std::array<Utils::Async::ProgressBarContext, 3> m_progress;

    std::list<std::unique_ptr<Viewers::Viewer>> m_listViewers;
    std::list<std::unique_ptr<Viewers::Viewer>> m_viewers;
    Viewers::Viewer* m_currentViewer = nullptr;
    uint32 m_nextViewerID = 0;
};

}

export namespace GW2Viewer::G { UI::Manager UI; }
