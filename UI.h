#pragma once
#include "GUID.h"
#include "IconsFontAwesome6.h"
#include "ProgressBarContext.h"

#include <imgui.h>

#include <list>
#include <memory>
#include <string>
#include <vector>

struct ArchiveFile;
struct ContentObject;

struct DrawTextureOptions
{
    std::vector<byte> const* Data;
    ImVec4 Color { 1, 1, 1, 1 };
    ImVec2 Size { };
    ImRect UV { 0, 0, 1, 1 };
    std::optional<ImRect> UV2;
    bool PreserveAspectRatio = true;
    bool FullPreviewOnHover = true;
    bool AdvanceCursor = true;
    bool ReserveSpace = false;
};
bool DrawTexture(uint32 textureFileID, DrawTextureOptions const& options = { });
struct DrawContentButtonOptions
{
    std::string_view Icon = ICON_FA_ARROW_RIGHT;
    std::string_view MissingTypeName = "???";
    std::string_view MissingContentName = "";
    struct CondenseContext
    {
        bool FullName = false;
        bool TypeName = false;
        bool Condense() { return !std::exchange(TypeName, std::exchange(FullName, true)); }
    };
    CondenseContext* SharedCondenseContext = nullptr;
};
void DrawContentButton(ContentObject* content, void const* id, DrawContentButtonOptions const& options = { });
struct DrawVoiceButtonOptions
{
    bool Selectable = false;
    bool MenuItem = false;
    uint32 VariantIndex = 0;
};
void DrawVoiceButton(uint32 voiceID, DrawVoiceButtonOptions const& options = { });
void DrawTextVoiceButton(uint32 textID, DrawVoiceButtonOptions const& options = { });
void DrawCopyButton(char const* name, std::string const& data, bool condition = true);
inline void DrawCopyButton(char const* name, std::wstring_view data, bool condition = true) { DrawCopyButton(name, data.size() <= 20 ? to_utf8(data) : to_utf8(std::format(L"{}...", std::wstring_view(data).substr(0, 20))), condition); }
inline void DrawCopyButton(char const* name, wchar_t const* data, bool condition = true) { DrawCopyButton(name, std::wstring_view(data), condition); }
template<typename T>
void DrawCopyButton(char const* name, T const& data, bool condition = true) { DrawCopyButton(name, std::format("{}", data), condition); }

struct EventID;

class UI
{
public:
    void Create();

    void DrawFrame();

    void OpenFile(ArchiveFile const& file, bool newTab = false, bool historyMove = false);
    void OpenContent(ContentObject& object, bool newTab = false, bool historyMove = false);
    void OpenConversation(uint32 conversationID, bool newTab = false, bool historyMove = false);
    void OpenEvent(EventID eventID, bool newTab = false, bool historyMove = false);
    void OpenWorldMap(bool newTab = false);

    std::string MakeDataLink(byte type, uint32 id);

    void PlayVoice(uint32 voiceID);

    void ExportData(std::span<byte const> data, std::filesystem::path const& path);

    void Defer(std::function<void()>&& func) { m_deferred.emplace_back(std::move(func)); }
    float DeltaTime() const { return m_deltaTime; }

    void ClearContentSortCache() { m_contentSortClearCache = true; }

    struct
    {
        ImFont* Default { };
        ImFont* GameText { };
        ImFont* GameTextItalic { };
        ImFont* GameHeading { };
        ImFont* GameHeadingItalic { };
    } Fonts;

private:
    std::list<std::function<void()>> m_deferred;

    float m_deltaTime = 1.0f;

    bool m_showOriginalNames = false;

    std::string m_fileFilterString;
    uint32 m_fileFilterID { };
    uint32 m_fileFilterRange { };

    std::string m_stringFilterString;
    std::optional<std::pair<int32, int32>> m_stringFilterID;
    uint32 m_stringFilterRange { };
    struct { bool Unencrypted = true, Encrypted = true, Decrypted = true, Empty = true; } m_stringFilters;
    enum class StringSort { ID, Text, DecryptionTime, Voice } m_stringSort { StringSort::ID };
    bool m_stringSortInvert { };

    int32 m_contentFilterType = 0;
    std::string m_contentFilterString;
    std::string m_contentFilterName;
    std::optional<GUID> m_contentFilterGUID;
    std::pair<uint32, uint32> m_contentFilterUID { -1, -1 };
    std::pair<uint32, uint32> m_contentFilterDataID { -1, -1 };
    enum class ContentSort { GUID, UID, DataID, Type, Name } m_contentSort { ContentSort::GUID };
    bool m_contentSortInvert { };
    bool m_contentSortClearCache = false;

    std::string m_conversationFilterString;
    std::optional<std::pair<int32, int32>> m_conversationFilterID;
    uint32 m_conversationFilterRange { };
    enum class ConversationSort { GenID, UID, StartingSpeakerName, StartingStateText, EncounteredTime } m_conversationSort { ConversationSort::UID };
    bool m_conversationSortInvert { };

    std::string m_eventFilterString;
    std::optional<std::pair<int32, int32>> m_eventFilterID;
    uint32 m_eventFilterRange { };
    struct { bool Normal = true, Group = true, Meta = true, Dungeon = true, NonEvent = true; } m_eventFilters;
    enum class EventSort { ID, Map, Type, Title, EncounteredTime } m_eventSort { EventSort::ID };
    bool m_eventSortInvert { };

    std::array<ProgressBarContext, 3> m_progress;

    struct Viewer
    {
        uint32 const ID;
        bool SetSelected = false;
        bool SetAfterCurrent = false;

        virtual ~Viewer() = default;

        virtual std::string Title() = 0;
        virtual void Draw() = 0;

    protected:
        Viewer(uint32 id, bool newTab) : ID(id), SetSelected(!newTab), SetAfterCurrent(newTab) { }
    };
    struct RawFileViewer;
    struct ContentViewer;
    struct ConversationViewer;
    struct EventViewer;
    struct MapLayoutViewer;
    std::list<std::unique_ptr<Viewer>> m_viewers;
    Viewer* m_currentViewer = nullptr;
    uint32 m_nextViewerID = 0;
    bool IsViewerRawFile(Viewer const* viewer, ArchiveFile const& file);

    static auto& GetFileViewerRegistry()
    {
        static std::unordered_map<fcc, std::function<RawFileViewer*(uint32 id, bool newTab, ArchiveFile const& file)>> instance { };
        return instance;
    }
    template<fcc FourCC>
    struct RegisterFileViewer
    {
        static bool Register();
        inline static bool Registered = Register();
    };
    template<fcc FourCC>
    struct FileViewer;
};

extern UI g_ui;
