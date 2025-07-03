export module GW2Viewer.UI.Viewers.EventViewer;
import GW2Viewer.Common;
import GW2Viewer.Content.Event;
import GW2Viewer.Data.Content;
import GW2Viewer.UI.Viewers.ViewerWithHistory;
import std;

export namespace GW2Viewer::UI::Viewers
{

struct EventViewer : ViewerWithHistory<EventViewer, Content::EventID>
{
    TargetType EventID;

    struct Cache
    {
        struct Data
        {
            uint32 SelectedVariant = 0;
            float Height = 0;

            float GetAndResetHeight() { return std::exchange(Height, 0); }
            void StoreHeight();
        };
        Data Event;
        std::vector<Data> Objectives;
    };
    std::map<Content::EventID, Cache> Cache;
    std::optional<std::pair<Content::EventID, std::optional<uint32>>> Selected;
    static inline bool Invert = false;
    static inline bool InDungeonMap = false;
    static inline float PreviewProgress = 0.75f;
    struct DrawProgressParams
    {
        uint32 DisplayedAsProgressBar;
        bool DefendDefault = false;
        uint32 Invert = false;
        uint32 InvertProgress = false;
        uint32 AgentName = 0;
        std::wstring_view AgentNameLiteral;
        uint32 AgentVerb = 0;
        uint32 Text = 0;
        uint32 TextDefault = 0;
        uint32 TextDefaultInverted = TextDefault;
        std::optional<uint32> WarningTime;
        uint32 TargetCount = 100;
        uint32 Count = (uint32)(TargetCount * PreviewProgress);
        float Progress = TargetCount ? (float)Count / (float)TargetCount : PreviewProgress;
        uint32 Icon = 0;
        uint32 IconInverted = Icon;
        uint32 IconFileID = 0;
        bool IconRight = true;
        bool HideIcon = false;
        std::optional<uint32> ProgressBarColor;
        Data::Content::ContentObject const* ProgressBarStyleDef = nullptr;
        uint32 FormatSex = 2;
    };

    EventViewer(uint32 id, bool newTab, TargetType eventID) : Base(id, newTab), EventID(eventID) { }

    TargetType GetCurrent() const override { return EventID; }
    bool IsCurrent(TargetType target) const override { return EventID == target; }

    std::string Title() override;
    void Draw() override;

    void DrawEvent(Content::EventID eventID);
    void DrawObjective(Content::Event::Objective const& objective, Cache::Data& cache, uint32 index, uint32* variant = nullptr, uint32 const* variantCount = nullptr);
};

}
