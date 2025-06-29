module;
#include "UI/ImGui/ImGui.h"
#include "Utils/Async.h"
//#include <cpp-base64/base64.h>
#include "dep/fmod/fmod.hpp"

module GW2Viewer.UI.Manager;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.Hash;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Content;
import GW2Viewer.Content.Conversation;
import GW2Viewer.Content.Event;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Content.Mangling;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Encryption.Asset;
import GW2Viewer.Data.Encryption.RC4;
import GW2Viewer.Data.External.Database;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Pack;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Viewers.ContentListViewer;
import GW2Viewer.UI.Viewers.ContentViewer;
import GW2Viewer.UI.Viewers.ConversationListViewer;
import GW2Viewer.UI.Viewers.ConversationViewer;
import GW2Viewer.UI.Viewers.EventListViewer;
import GW2Viewer.UI.Viewers.EventViewer;
import GW2Viewer.UI.Viewers.FileListViewer;
import GW2Viewer.UI.Viewers.FileViewer;
import GW2Viewer.UI.Viewers.FileViewers;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Viewers.MapLayoutViewer;
import GW2Viewer.UI.Viewers.StringListViewer;
import GW2Viewer.UI.Windows.Demangle;
import GW2Viewer.UI.Windows.MigrateContentTypes;
import GW2Viewer.UI.Windows.Notes;
import GW2Viewer.UI.Windows.Parse;
import GW2Viewer.UI.Windows.Settings;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Base64;
import GW2Viewer.Utils.ConstString;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Exception;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.Scan;
import GW2Viewer.Utils.String;
import GW2Viewer.Utils.Visitor;
import std;
import magic_enum;
import <experimental/generator>;
import <gsl/gsl>;

using namespace std::chrono_literals;

namespace UI
{

void Manager::Load()
{
    ImGuiIO& io = I::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    //io.ConfigWindowsMoveFromTitleBarOnly = true;

    io.Fonts->AddFontDefault();
    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphMinAdvanceX = 10.0f;
    ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    builder.AddRanges(std::array<ImWchar, 3> { 0x2000, 0x2100, 0 }.data());
    builder.BuildRanges(&ranges);
    auto loadFont = [&](const char* filename, float size)
    {
        static constexpr ImWchar faRanges[] { ICON_MIN_FA, ICON_MAX_FA, 0 };
        auto font = io.Fonts->AddFontFromFileTTF(std::format(R"(Resources\Fonts\{})", filename).c_str(), size, nullptr, ranges.Data);
        io.Fonts->AddFontFromFileTTF(R"(Resources\Fonts\)" FONT_ICON_FILE_NAME_FAS, 10.0f, &config, faRanges);
        return font;
    };
    Fonts.Default = loadFont("Roboto-Regular.ttf", 15.0f);
    Fonts.GameText = loadFont("trebuc.ttf", 15.5f);
    Fonts.GameTextItalic = loadFont("trebucit.ttf", 15.5f);
    Fonts.GameHeading = loadFont("menomonia.ttf", 18.0f);
    Fonts.GameHeadingItalic = loadFont("menomonia-italic.ttf", 18.0f);
    io.Fonts->Build();
    io.FontDefault = Fonts.Default;

    ImVec4* colors = I::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    //colors[ImGuiCol_DockingPreview] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    //colors[ImGuiCol_DockingEmptyBg] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

    colors[ImGuiCol_PlotHistogram] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

    ImGuiStyle& style = I::GetStyle();
    style.WindowPadding = ImVec2(8.00f, 8.00f);
    style.FramePadding = ImVec2(5.00f, 2.00f);
    style.CellPadding = ImVec2(6.00f, 6.00f);
    style.ItemSpacing = ImVec2(6.00f, 6.00f);
    style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
    style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
    style.IndentSpacing = 25;
    style.ScrollbarSize = 15;
    style.GrabMinSize = 10;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;
    style.TabBorderSize = 1;
    style.WindowRounding = 7;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 9;
    style.GrabRounding = 3;
    style.LogSliderDeadzone = 4;
    style.TabRounding = 4;

    m_listViewers.emplace_back(std::make_unique<Viewers::FileListViewer>(m_nextViewerID++, false));
    m_listViewers.emplace_back(std::make_unique<Viewers::StringListViewer>(m_nextViewerID++, false));
    m_listViewers.emplace_back(std::make_unique<Viewers::ContentListViewer>(m_nextViewerID++, false));
    m_listViewers.emplace_back(std::make_unique<Viewers::ConversationListViewer>(m_nextViewerID++, false));
    m_listViewers.emplace_back(std::make_unique<Viewers::EventListViewer>(m_nextViewerID++, false));
}

void Manager::Update()
{
    m_now = std::chrono::high_resolution_clock::now();
    static auto prev = m_now;
    m_deltaTime = std::max(0.001f, std::chrono::duration_cast<std::chrono::microseconds>(m_now - prev).count() / 1000000.0f);
    prev = m_now;

    while (!m_deferred.empty())
    {
        m_deferred.front()();
        m_deferred.pop_front();
    }

    static bool needInitialSettings = G::Config.GameExePath.empty() || G::Config.GameDatPath.empty();

    if (G::Config.ShowImGuiDemo)
    {
        I::SetNextWindowPos(ImVec2(500, 0), ImGuiCond_FirstUseEver);
        I::ShowDemoWindow(&G::Config.ShowImGuiDemo);
    }
    
    std::ranges::for_each(G::Windows::GetAllWindows(), &Windows::Window::Update);
    if (G::Windows::Settings.Accepted)
        needInitialSettings = false;
    else if (needInitialSettings)
        return G::Windows::Settings.Show();

    if (G::Config.MainWindowFullScreen)
    {
        I::SetNextWindowPos(I::GetMainViewport()->WorkPos);
        I::SetNextWindowSize(I::GetMainViewport()->WorkSize);
    }
    if (scoped::Window("GW2Browser", nullptr, ImGuiWindowFlags_MenuBar | (G::Config.MainWindowFullScreen ? ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove : 0)))
    {
        if (scoped::MenuBar())
        {
            if (scoped::Menu("Config"))
            {
                if (I::MenuItem("Load"))
                    G::Config.Load();
                if (I::MenuItem("Save"))
                    G::Config.Save();
            }
            if (scoped::Menu("View"))
            {
                I::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
                if (I::MenuItem("Show Original Names", nullptr, &G::Config.ShowOriginalNames))
                    std::ranges::for_each(G::Viewers::ListViewers<Viewers::ContentListViewer>, &Viewers::ContentListViewer::ClearCache);
                I::MenuItem("Show <c=#CCF>Valid Raw Pointers</c>", nullptr, &G::Config.ShowValidRawPointers);
                I::MenuItem("Show Content Symbol <c=#8>Name</c> Before <c=#4>Type</c>", nullptr, &G::Config.ShowContentSymbolNameBeforeType);
                I::MenuItem("Display Content Layout As  " ICON_FA_FOLDER_TREE " Tree", nullptr, &G::Config.TreeContentStructLayout);
                I::MenuItem("Full Screen Window", nullptr, &G::Config.MainWindowFullScreen);
                I::MenuItem("Open ImGui Demo Window", nullptr, &G::Config.ShowImGuiDemo);
                I::MenuItem("Open Parse Window", nullptr, &G::Windows::Parse.GetShown());
                I::MenuItem("Open Demangle Window", nullptr, &G::Windows::Demangle.GetShown());
                I::MenuItem("Open Notes Window", nullptr, &G::Windows::Notes.GetShown());
                I::MenuItem("Open Settings Window", nullptr, &G::Windows::Settings.GetShown());
                I::PopItemFlag();
            }
            if (scoped::Menu("Language"))
            {
                static constexpr std::pair<Language, char const*> languages[]
                {
                    { Language::English, "English" },
                    { Language::Korean, "Korean" },
                    { Language::French, "French" },
                    { Language::German, "German" },
                    { Language::Spanish, "Spanish" },
                    { Language::Chinese, "Chinese" },
                };
                I::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
                for (auto const& [lang, text] : languages)
                    if (I::MenuItem(text, nullptr, G::Config.Language == lang))
                        G::Config.Language = lang;
                I::PopItemFlag();
            }
            if (scoped::Menu("Tools"))
            {
                if (I::MenuItem("Export Content Files"))
                    for (auto const fileID : G::Game.Content.GetFileIDs())
                        if (auto data = G::Game.Archive.GetFile(fileID); !data.empty())
                            ExportData(data, std::format(R"(Export\Game Content\{}.cntc)", fileID));
                I::MenuItem("Migrate Content Types", nullptr, &G::Windows::MigrateContentTypes.GetShown());
            }
            for (auto const& progress : m_progress)
            {
                if (auto lock = progress.Lock(); progress.IsRunning())
                {
                    if (progress.IsIndeterminate())
                    {
                        I::SetCursorPosY(I::GetCursorPosY() + 2);
                        I::IndeterminateProgressBar({ 100, 16 });
                        I::SameLine();
                        I::TextUnformatted(progress.GetDescription().c_str());
                    }
                    else
                    {
                        auto [p, current, total] = progress.GetProgress();
                        I::SetCursorPosY(I::GetCursorPosY() + 2);
                        I::ProgressBar(p, { 100, 16 });
                        I::SameLine();
                        I::Text("%zu / %zu", current, total);
                        I::SameLine();
                        I::TextUnformatted(progress.GetDescription().c_str());
                    }
                }
            }
        }
        if (scoped::Child("SourcesPane", { 250, 0 }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX))
        {
            if (scoped::TabBar("Tabs", ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
            {
                auto tabBar = I::GetCurrentTabBar();
                int selectedTabOrder = -1;
                std::unique_ptr<Viewers::Viewer> const* toRemove = nullptr;
                for (auto& viewer : m_listViewers)
                {
                    bool open = true;
                    if (scoped::TabItem(std::format("{}###Viewer-{}", viewer->Title(), viewer->ID).c_str(), &open, viewer->SetSelected ? ImGuiTabItemFlags_SetSelected : 0))
                        viewer->Draw();
                    if (auto tab = I::TabBarGetCurrentTab(tabBar))
                    {
                        if (tab->ID == tabBar->SelectedTabId)
                            selectedTabOrder = I::TabBarGetTabOrder(tabBar, tab);
                        if (viewer->SetAfterCurrent)
                            if (int offset = selectedTabOrder - I::TabBarGetTabOrder(tabBar, tab) + 1)
                                I::TabBarQueueReorder(I::GetCurrentTabBar(), tab, offset);
                    }
                    viewer->SetSelected = false;
                    viewer->SetAfterCurrent = false;
                    if (!open)
                        toRemove = &viewer;
                }
                if (toRemove)
                    m_listViewers.erase(std::ranges::find(m_listViewers, *toRemove));


                // TODO: if (static bool focus = true; scoped::TabItem(ICON_FA_TEXT " Strings", nullptr, std::exchange(focus, false) ? ImGuiTabItemFlags_SetSelected : 0));
                
                if (scoped::TabItem(ICON_FA_BOOKMARK " Bookmarks"))
                {
                    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, { I::GetStyle().FramePadding.x, 0 }))
                    if (scoped::Table("Table", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable))
                    {
                        I::TableSetupColumn("Bookmark", ImGuiTableColumnFlags_WidthStretch);
                        I::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
                        I::TableSetupScrollFreeze(0, 1);
                        I::TableHeadersRow();

                        for (auto const& bookmark : G::Config.BookmarkedContentObjects)
                        {
                            I::TableNextRow();
                            I::TableNextColumn(); Controls::ContentButton(G::Game.Content.GetByGUID(bookmark.Value), &bookmark, { .MissingContentName = "CONTENT OBJECT MISSING" });
                            I::TableNextColumn(); I::TextUnformatted(Utils::Format::DurationShortColored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - bookmark.Time)).c_str());
                        }
                    }
                }
                if (scoped::TabItem(ICON_FA_WRENCH " Tools"))
                {
                    if (scoped::WithStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0, I::GetStyle().ButtonTextAlign.y }))
                    {
                        if (I::Button(ICON_FA_GLOBE " World Map", { -FLT_MIN, 0 }); auto const button = I::IsItemMouseClickedWith(ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
                            OpenWorldMap(button & ImGuiButtonFlags_MouseButtonMiddle);
                    }
                }
            }
        }
        if (I::SameLine(); scoped::Child("ViewerPane", { }, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Border))
        {
            if (scoped::TabBar("Tabs", ImGuiTabBarFlags_TabListPopupButton/* | ImGuiTabBarFlags_AutoSelectNewTabs*/ | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_Reorderable))
            {
                auto tabBar = I::GetCurrentTabBar();
                int selectedTabOrder = -1;
                std::unique_ptr<Viewers::Viewer> const* toRemove = nullptr;
                for (auto& viewer : m_viewers)
                {
                    bool open = true;
                    if (scoped::TabItem(std::format("{}###Viewer-{}", viewer->Title(), viewer->ID).c_str(), &open, viewer->SetSelected ? ImGuiTabItemFlags_SetSelected : 0))
                    {
                        if (open)
                            m_currentViewer = viewer.get();

                        viewer->Draw();
                    }
                    if (auto tab = I::TabBarGetCurrentTab(tabBar))
                    {
                        if (tab->ID == tabBar->SelectedTabId)
                            selectedTabOrder = I::TabBarGetTabOrder(tabBar, tab);
                        if (viewer->SetAfterCurrent)
                            if (int offset = selectedTabOrder - I::TabBarGetTabOrder(tabBar, tab) + 1)
                                I::TabBarQueueReorder(I::GetCurrentTabBar(), tab, offset);
                    }
                    viewer->SetSelected = false;
                    viewer->SetAfterCurrent = false;
                    if (!open)
                        toRemove = &viewer;
                }
                if (toRemove)
                {
                    if (m_currentViewer == toRemove->get())
                        m_currentViewer = nullptr;
                    m_viewers.erase(std::ranges::find(m_viewers, *toRemove));
                }
            }
        }
    }

    G::Game.Texture.UploadToGPU();
    static bool firstTime = [&]
    {
        if (!G::Config.GameDatPath.empty())
            G::Game.Archive.Add(Data::Archive::Kind::Game, G::Config.GameDatPath);
        if (!G::Config.LocalDatPath.empty())
            G::Game.Archive.Add(Data::Archive::Kind::Local, G::Config.LocalDatPath);
        m_progress[0].Run([this](ProgressBarContext& progress)
        {
            progress.Start("Preparing decryption key storage");
            if (!G::Config.DecryptionKeysPath.empty())
            {
                auto const extension = std::wstring(std::from_range, std::filesystem::path(G::Config.DecryptionKeysPath).extension().wstring() | std::views::transform(towlower));
                if (extension == L".sqlite")
                {
                    G::Database.Load(G::Config.DecryptionKeysPath, progress);
                }
                if (extension == L".txt")
                {
                    std::filesystem::path const path = LR"(E:\Program Files\Guild Wars 2\addons\arcdps\arcdps_chatlog_keys.txt)";
                    progress.Start("Reading string decryption keys", file_size(path));
                    std::ifstream file(path);
                    std::string buffer;
                    uint32 session = 0;
                    std::chrono::local_seconds time;
                    while (std::getline(file, buffer))
                    {
                        uint32 stringID;
                        uint64 key;
                        if (std::string timeString; Utils::Scan::Into(buffer, "; Session: {:[^\r\n]}", timeString))
                        {
                            std::istringstream(timeString) >> std::chrono::parse("%F %T", time);
                            ++session;
                        }
                        if (Utils::Scan::Into(buffer, "{} = {:x}", stringID, key))
                            G::Game.Encryption.AddTextKeyInfo(stringID, { .Key = key, .Time = std::chrono::system_clock::to_time_t(std::chrono::current_zone()->to_sys(time)), .Session = session });

                        progress = file.tellg();
                    }
                }
                if (extension == L".txt")
                {
                    std::filesystem::path const path = LR"(E:\Program Files\Guild Wars 2\addons\arcdps\arcdps_chatlog_asset_keys.txt)";
                    progress.Start("Reading asset decryption keys", file_size(path));
                    std::ifstream file(path);
                    std::string buffer;
                    while (std::getline(file, buffer))
                    {
                        uint32 assetType, assetID;
                        uint64 key;
                        if (Utils::Scan::Into(buffer, "{} {} = {:x}", assetType, assetID, key))
                            G::Game.Encryption.AddAssetKey((Data::Encryption::AssetType)assetType, assetID, key);

                        progress = file.tellg();
                    }
                }
            }

            G::Game.Archive.Load(progress);
            auto sourcePtr = G::Game.Archive.GetSource();
            if (!sourcePtr)
                return;
            auto& source = *sourcePtr;

            progress.Start("Creating file list");
            std::ranges::for_each(G::Viewers::ListViewers<Viewers::FileListViewer>, &Viewers::FileListViewer::UpdateFilter);

            // Wait for PackFile layouts to load before continuing
            while (!G::Game.Pack.IsLoaded())
                std::this_thread::sleep_for(50ms);

            // Can't parallelize currently, Archive supports only single-thread file loading

            G::Game.Text.Load(source, progress);
            progress.Start("Creating string list");
            std::ranges::for_each(G::Viewers::ListViewers<Viewers::StringListViewer>, &Viewers::StringListViewer::UpdateFilter);
            progress.Start("Creating conversation list");
            std::ranges::for_each(G::Viewers::ListViewers<Viewers::ConversationListViewer>, &Viewers::ConversationListViewer::UpdateSearch);
            progress.Start("Creating event list");
            std::ranges::for_each(G::Viewers::ListViewers<Viewers::EventListViewer>, &Viewers::EventListViewer::UpdateFilter);

            G::Game.Voice.Load(source, progress);
            m_progress[2].Run([=, &source](ProgressBarContext& progress)
            {
                G::Game.Content.Load(source, progress);
                std::ranges::for_each(G::Viewers::ListViewers<Viewers::ContentListViewer>, [](Viewers::ContentListViewer* viewer) { viewer->UpdateFilter(); });

                progress.Start("Processing content types for migration");
                if (!G::Config.LastNumContentTypes)
                    G::Config.LastNumContentTypes = G::Game.Content.GetNumTypes();
                if (G::Config.LastNumContentTypes == G::Game.Content.GetNumTypes())
                {
                    for (auto const& type : G::Game.Content.GetTypes())
                    {
                        auto const itr = G::Config.TypeInfo.find(type->Index);
                        if (itr == G::Config.TypeInfo.end())
                            continue;

                        Data::Content::TypeInfo& typeInfo = itr->second;
                        if (typeInfo.Examples.empty() && !type->Objects.empty() && type->GUIDOffset >= 0)
                            typeInfo.Examples.insert_range(type->Objects | std::views::take(5) | std::views::transform([](Data::Content::ContentObject const* content) { return *content->GetGUID(); }));
                    }
                }
                else
                    G::Windows::MigrateContentTypes.Show();
            });
        });
        m_progress[1].Run([](ProgressBarContext& progress)
        {
            if (!G::Config.GameExePath.empty())
                G::Game.Pack.Load(G::Config.GameExePath, progress);
        });
        return true;
    }();
}

void Manager::OpenFile(Data::Archive::File const& file, bool newTab, bool historyMove)
{
    static auto init = [](uint32 id, bool newTab, Data::Archive::File const& file)
    {
        Viewers::FileViewer* result = nullptr;
        if (auto const data = file.Source.get().Archive.GetFile(file.ID); data.size() >= 4) // TODO: Refactor to avoid copying
        {
            auto&& registry = Viewers::FileViewers::GetRegistry();
            if (auto const itr = registry.find(*(fcc const*)data.data()); itr != registry.end())
                result = itr->second(id, newTab, file);
        }
        if (!result)
            result = new Viewers::FileViewer(id, newTab, file);
        result->Initialize();
        return result;
    };
    Defer([=]
    {
        if (I::GetIO().KeyAlt)
        {
            auto data = file.Source.get().Archive.GetFile(file.ID);
            ExportData(data, std::format(R"(Export\{})", file.ID));
            G::Game.Texture.Load(file.ID, { .DataSource = &data, .Export = true });
            return;
        }

        if (auto* currentViewer = dynamic_cast<Viewers::FileViewer*>(m_currentViewer); currentViewer && !newTab)
        {
            if (currentViewer->File == file)
                return;

            auto const id = currentViewer->ID;
            auto historyPrev = std::move(currentViewer->HistoryPrev);
            auto historyNext = std::move(currentViewer->HistoryNext);
            if (!historyMove)
            {
                historyPrev.emplace(currentViewer->File);
                historyNext = { };
            }

            //currentViewer->~FileViewer();
            //new(currentViewer) FileViewer(id, newTab, file);

            auto const itr = std::ranges::find(m_viewers, m_currentViewer, [](auto const& ptr) { return ptr.get(); });
            itr->reset(init(id, newTab, file));
            currentViewer = dynamic_cast<Viewers::FileViewer*>(m_currentViewer = itr->get());

            currentViewer->HistoryPrev = std::move(historyPrev);
            currentViewer->HistoryNext = std::move(historyNext);
        }
        else
            m_viewers.emplace_back(init(m_nextViewerID++, newTab, file));
    });
}
void Manager::OpenContent(Data::Content::ContentObject& object, bool newTab, bool historyMove)
{
    Defer([=, &object]
    {
        if (auto* currentViewer = dynamic_cast<Viewers::ContentViewer*>(m_currentViewer); currentViewer && !newTab)
        {
            if (&currentViewer->Content == &object)
                return;

            auto const id = currentViewer->ID;
            auto historyPrev = std::move(currentViewer->HistoryPrev);
            auto historyNext = std::move(currentViewer->HistoryNext);
            if (!historyMove)
            {
                historyPrev.emplace(&currentViewer->Content);
                historyNext = { };
            }
            currentViewer->~ContentViewer();
            new(currentViewer) Viewers::ContentViewer(id, newTab, object);
            currentViewer->HistoryPrev = std::move(historyPrev);
            currentViewer->HistoryNext = std::move(historyNext);
        }
        else
            m_viewers.emplace_back(new Viewers::ContentViewer(m_nextViewerID++, newTab, object));
    });
}
void Manager::OpenConversation(uint32 conversationID, bool newTab, bool historyMove)
{
    Defer([=]
    {
        if (auto* currentViewer = dynamic_cast<Viewers::ConversationViewer*>(m_currentViewer); currentViewer && !newTab)
        {
            if (currentViewer->ConversationID == conversationID)
                return;

            auto const id = currentViewer->ID;
            auto historyPrev = std::move(currentViewer->HistoryPrev);
            auto historyNext = std::move(currentViewer->HistoryNext);
            if (!historyMove)
            {
                historyPrev.emplace(currentViewer->ConversationID);
                historyNext = { };
            }
            currentViewer->~ConversationViewer();
            new(currentViewer) Viewers::ConversationViewer(id, newTab, conversationID);
            currentViewer->HistoryPrev = std::move(historyPrev);
            currentViewer->HistoryNext = std::move(historyNext);
        }
        else
            m_viewers.emplace_back(new Viewers::ConversationViewer(m_nextViewerID++, newTab, conversationID));
    });
}
void Manager::OpenEvent(Content::EventID eventID, bool newTab, bool historyMove)
{
    Defer([=]
    {
        if (auto* currentViewer = dynamic_cast<Viewers::EventViewer*>(m_currentViewer); currentViewer && !newTab)
        {
            if (currentViewer->EventID == eventID)
                return;

            auto const id = currentViewer->ID;
            auto historyPrev = std::move(currentViewer->HistoryPrev);
            auto historyNext = std::move(currentViewer->HistoryNext);
            if (!historyMove)
            {
                historyPrev.emplace(currentViewer->EventID);
                historyNext = { };
            }
            currentViewer->~EventViewer();
            new(currentViewer) Viewers::EventViewer(id, newTab, eventID);
            currentViewer->HistoryPrev = std::move(historyPrev);
            currentViewer->HistoryNext = std::move(historyNext);
        }
        else
            m_viewers.emplace_back(new Viewers::EventViewer(m_nextViewerID++, newTab, eventID));
    });
}
void Manager::OpenWorldMap(bool newTab)
{
    Defer([=]
    {
        m_viewers.emplace_back(new Viewers::MapLayoutViewer(m_nextViewerID++, newTab));
    });
}

std::string Manager::MakeDataLink(byte type, uint32 id)
{
    switch (type)
    {
        case 2:
        {
#pragma pack(push, 1)
            struct
            {
                byte Type;
                byte Count;
                uint32 ID;
                byte Payload[4 + 4 + 4 + 8 + 8];
            } dataLink { type, 1, id };
#pragma pack(pop)
            static_assert(sizeof(dataLink) == 6 + 28);

            enum : uint32
            {
                HAS_SKIN            = 0x80000000,
                HAS_UPGRADE_1       = 0x40000000,
                HAS_UPGRADE_2       = 0x20000000,
                HAS_KEY_NAME        = 0x10000000,
                HAS_KEY_DESCRIPTION = 0x08000000,
            };

            uint32 payloadSize = 0;
            if (Data::Content::ContentObject* item = G::Game.Content.GetByDataID(Content::ItemDef, id))
            {
                if (auto const key = G::Game.Encryption.GetTextKey((*item)["Name"]); key && *key)
                {
                    dataLink.ID |= HAS_KEY_NAME;
                    *(uint64*)&dataLink.Payload[payloadSize] = *key;
                    payloadSize += sizeof(uint64);
                }
                if (auto const key = G::Game.Encryption.GetTextKey((*item)["Description"]); key && *key)
                {
                    dataLink.ID |= HAS_KEY_DESCRIPTION;
                    *(uint64*)&dataLink.Payload[payloadSize] = *key;
                    payloadSize += sizeof(uint64);
                }
            }
            return std::format("[&{}]", Utils::Base64::Encode({ (char const*)&dataLink, offsetof(decltype(dataLink), Payload) + payloadSize }));
        }
        default:
        {
#pragma pack(push, 1)
            struct
            {
                byte Type;
                uint32 ID;
            } dataLink { type, id };
#pragma pack(pop)
            static_assert(sizeof(dataLink) == 5);
            return std::format("[&{}]", Utils::Base64::Encode({ (char const*)&dataLink, sizeof(dataLink) }));
        }
    }
}

void Manager::PlayVoice(uint32 voiceID)
{
    auto const data = G::Game.Voice.Get(voiceID, G::Config.Language);
    if (data.empty())
        return;

    static std::unique_ptr<FMOD::System, decltype([](FMOD::System* system) { system->release(); })> system([]() -> FMOD::System*
    {
        FMOD::System* system;
        if (System_Create(&system) != FMOD_OK)
            return nullptr;
        if (system->init(32, FMOD_INIT_NORMAL, nullptr) != FMOD_OK)
            return nullptr;
        return system;
    }());

    if (!system)
        return;

    FMOD_CREATESOUNDEXINFO info
    {
        .cbsize = sizeof(FMOD_CREATESOUNDEXINFO),
        .length = (uint32)data.size(),
    };
    try
    {
        static FMOD::Channel* channel = nullptr;
        if (channel)
            std::exchange(channel, nullptr)->stop();

        FMOD::Sound* sound;
        if (system->createSound((char const*)data.data(), FMOD_OPENMEMORY, &info, &sound) != FMOD_OK)
        {
            auto const key = G::Game.Encryption.GetAssetKey(Data::Encryption::AssetType::Voice, voiceID);
            if (!key)
                return;

            std::vector encrypted { std::from_range, data };
            Data::Encryption::RC4(Data::Encryption::RC4::MakeKey(*key)).Crypt(encrypted);
            if (system->createSound((char const*)encrypted.data(), FMOD_OPENMEMORY, &info, &sound) != FMOD_OK)
                return;

            if (I::GetIO().KeyAlt)
                return ExportData(encrypted, std::format(R"(Export\Voice\English\{}.mp3)", voiceID));
        }

        if (I::GetIO().KeyAlt)
            return ExportData(data, std::format(R"(Export\Voice\English\{}.mp3)", voiceID));

        system->playSound(sound, nullptr, false, &channel);
    }
    catch(...) { }
}

void Manager::ExportData(std::span<byte const> data, std::filesystem::path const& path)
{
    create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write((char const*)data.data(), data.size());
}

}
