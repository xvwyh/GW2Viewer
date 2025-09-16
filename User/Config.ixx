module;
#include "Utils/Scan.h"

export module GW2Viewer.User.Config;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.JSON;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Content;
import std;
#include "Macros.h"

export namespace GW2Viewer::User
{

struct Config
{
    void Load();
    void Save();

    template<typename T>
    struct Bookmark
    {
        Time::Point Time;
        T Value;

        NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT(Bookmark
            , Time
            , Value
        )

        std::strong_ordering operator<=>(Bookmark const&) const = default;
    };

    std::string GameExePath;
    std::string GameDatPath;
    std::string LocalDatPath;
    std::string DecryptionKeysPath;
    Language Language = Language::English;

    struct UI
    {
        struct Viewers
        {
            struct ContentListViewer
            {
                bool AutoExpandSearchResults = true;
                uint32 AutoExpandSearchMaxResults = 5;
                bool AutoOpenSearchResult = false;
                bool AutoOpenSearchResultInBackgroundTab = false;
                bool DrawTreeLines = true;
                bool DrawSeparatorsBetweenReleases = false;
                bool HorizontalScroll = false;
                bool HorizontalScrollAutoIndent = false;
                bool HorizontalScrollAutoContent = false;

                NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT(ContentListViewer
                    , AutoExpandSearchResults
                    , AutoExpandSearchMaxResults
                    , AutoOpenSearchResult
                    , AutoOpenSearchResultInBackgroundTab
                    , DrawTreeLines
                    , DrawSeparatorsBetweenReleases
                    , HorizontalScroll
                    , HorizontalScrollAutoIndent
                    , HorizontalScrollAutoContent
                )
            } ContentListViewer;

            NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT(Viewers
                , ContentListViewer
            )
        } Viewers;

        NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT(UI
            , Viewers
        )
    } UI;

    bool ShowImGuiDemo = false;
    bool ShowOriginalNames = false;
    bool ShowValidRawPointers = false;
    bool ShowContentSymbolNameBeforeType = false;
    bool TreeContentStructLayout = false;
    std::string Notes;
    std::wstring BruteforceDictionary;
    std::set<Bookmark<GUID>> BookmarkedContentObjects;
    std::map<uint32, std::map<uint32, std::string>> ConversationScriptedStartSituations;

    std::map<uint32, Data::Content::TypeInfo> TypeInfo;
    std::map<std::string, Data::Content::TypeInfo::StructLayout> SharedTypes;
    std::map<std::string, Data::Content::TypeInfo::Enum> SharedEnums;
    std::map<std::wstring, std::wstring> ContentNamespaceNames;
    std::map<GUID, std::wstring> ContentObjectNames;
    uint32 LastNumContentTypes = 0;

    NLOHMANN_DEFINE_TYPE_ORDERED_INTRUSIVE_WITH_DEFAULT(Config
        , GameExePath
        , GameDatPath
        , LocalDatPath
        , DecryptionKeysPath
        , Language

        , UI

        , ShowImGuiDemo
        , ShowOriginalNames
        , ShowValidRawPointers
        , ShowContentSymbolNameBeforeType
        , TreeContentStructLayout
        , Notes
        , BruteforceDictionary
        , BookmarkedContentObjects
        , ConversationScriptedStartSituations

        , TypeInfo
        , SharedTypes
        , SharedEnums
        , ContentNamespaceNames
        , ContentObjectNames
        , LastNumContentTypes
    )
    void FinishLoading()
    {
    }
};

}

export namespace GW2Viewer::G { User::Config Config; }
