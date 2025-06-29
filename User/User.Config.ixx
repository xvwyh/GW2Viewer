module;
#include "Common/Common.JSON.h"
#include "Utils/Utils.Scan.h"

export module GW2Viewer.User.Config;
import GW2Viewer.Common;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.JSON;
import GW2Viewer.Data.Content;
import std;

export namespace User
{

struct Config
{
    void Load();
    void Save();

    template<typename T>
    struct Bookmark
    {
        std::chrono::system_clock::time_point Time;
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
    bool ShowImGuiDemo = false;
    bool ShowOriginalNames = false;
    bool ShowValidRawPointers = false;
    bool ShowContentSymbolNameBeforeType = false;
    bool TreeContentStructLayout = false;
    bool MainWindowFullScreen = true;
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
        , ShowImGuiDemo
        , ShowOriginalNames
        , ShowValidRawPointers
        , ShowContentSymbolNameBeforeType
        , TreeContentStructLayout
        , MainWindowFullScreen
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

export namespace G { User::Config Config; }
