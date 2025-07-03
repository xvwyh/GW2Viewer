export module GW2Viewer.UI.Viewers.ConversationViewer;
import GW2Viewer.Common;
import GW2Viewer.UI.Viewers.ViewerWithHistory;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct ConversationViewer : ViewerWithHistory<ConversationViewer, uint32>
{
    TargetType ConversationID;

    std::optional<uint32> EditingScriptedStartTransitionStateID;
    bool EditingScriptedStartTransitionFocus = false;

    ConversationViewer(uint32 id, bool newTab, TargetType conversationID) : Base(id, newTab), ConversationID(conversationID) { }

    TargetType GetCurrent() const override { return ConversationID; }
    bool IsCurrent(TargetType target) const override { return ConversationID == target; }

    std::string Title() override
    {
        return std::format("<c=#4>Conversation #</c>{}", ConversationID);
    }
    void Draw() override;
};

}
