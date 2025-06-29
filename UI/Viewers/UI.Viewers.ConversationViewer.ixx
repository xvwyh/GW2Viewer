export module GW2Viewer.UI.Viewers.ConversationViewer;
import GW2Viewer.Common;
import GW2Viewer.UI.Viewers.Viewer;
import std;

export namespace UI::Viewers
{

struct ConversationViewer : Viewer
{
    uint32 ConversationID;
    std::stack<uint32> HistoryPrev;
    std::stack<uint32> HistoryNext;

    std::optional<uint32> EditingScriptedStartTransitionStateID;
    bool EditingScriptedStartTransitionFocus = false;

    ConversationViewer(uint32 id, bool newTab, uint32 conversationID) : Viewer(id, newTab), ConversationID(conversationID) { }

    std::string Title() override
    {
        return std::format("<c=#4>Conversation #</c>{}", ConversationID);
    }
    void Draw() override;
};

}
