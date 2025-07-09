module;
#include "UI/ImGui/ImGui.h"
//#include <cpp-base64/base64.h>
#include "dep/fmod/fmod.hpp"

module GW2Viewer.UI.Manager;
import GW2Viewer.Content;
import GW2Viewer.Data.Encryption.Asset;
import GW2Viewer.Data.Encryption.RC4;
import GW2Viewer.Data.External.Database;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Viewers.ContentListViewer;
import GW2Viewer.UI.Viewers.ConversationListViewer;
import GW2Viewer.UI.Viewers.EventListViewer;
import GW2Viewer.UI.Viewers.FileListViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Viewers.MapLayoutViewer;
import GW2Viewer.UI.Viewers.StringListViewer;
import GW2Viewer.UI.Viewers.ViewerRegistry;
import GW2Viewer.UI.Windows.Demangle;
import GW2Viewer.UI.Windows.MigrateContentTypes;
import GW2Viewer.UI.Windows.Notes;
import GW2Viewer.UI.Windows.Parse;
import GW2Viewer.UI.Windows.Settings;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Base64;
import GW2Viewer.Utils.Scan;
import std;
import magic_enum;

using namespace std::chrono_literals;

namespace GW2Viewer::UI
{

void Manager::Load()
{
    ImGuiIO& io = I::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigErrorRecoveryEnableAssert = false;
    //io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDockingTransparentPayload = true;

    io.Fonts->AddFontDefault();
    auto loadFont = [&](const char* filename, float size)
    {
        static constexpr ImWchar faRanges[] { ICON_MIN_FA, ICON_MAX_FA, 0 };
        {
            ImFontConfig config;
            config.GlyphExcludeRanges = faRanges;
            io.Fonts->AddFontFromFileTTF(std::format(R"(Resources\Fonts\{})", filename).c_str(), size, &config);
        }
        {
            ImFontConfig config;
            config.MergeMode = true;
            config.GlyphExcludeRanges = faRanges;
            io.Fonts->AddFontFromFileTTF(R"(Resources\Fonts\NotoSansSC-Regular.ttf)", size, &config); // Fallback for Simplified Chinese
        }
        {
            ImFontConfig config;
            config.MergeMode = true;
            config.PixelSnapH = true;
            config.GlyphMinAdvanceX = 10.0f;
            return io.Fonts->AddFontFromFileTTF(R"(Resources\Fonts\)" FONT_ICON_FILE_NAME_FAS, 10.0f, &config);
        }
    };
    Fonts.Default = loadFont("Roboto-Regular.ttf", 15.0f);
    Fonts.GameText = loadFont("trebuc.ttf", 14.725f);
    Fonts.GameTextItalic = loadFont("trebucit.ttf", 14.725f);
    Fonts.GameHeading = loadFont("menomonia.ttf", 18.0f);
    Fonts.GameHeadingItalic = loadFont("menomonia-italic.ttf", 18.0f);
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
    colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
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
    colors[ImGuiCol_NavCursor] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.75f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.75f);

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

    for (auto const name : { "Files", "Strings", "Content", "Conversations", "Events", "Bookmarks" })
        m_listViewers.emplace_back(Viewers::ViewerRegistry::GetByName(name)->Constructor(m_nextViewerID++, false));

    for (auto const& viewer : m_listViewers)
        viewer->SetSelected = dynamic_cast<Viewers::StringListViewer*>(viewer.get());
}

void Manager::Unload()
{
    m_currentViewer = nullptr;
    m_viewers.clear();
    m_listViewers.clear();
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

    ImGuiID dockspace = I::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_NoCloseButton);
    static ImGuiWindowClass windowClassViewer;
    static ImGuiID left, center;
    static bool dockSpaceInited = [dockspace]
    {
        windowClassViewer.DockingAlwaysTabBar = true;

        I::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.4f, &left, &center);
        return true;
    }();

    if (G::Config.ShowImGuiDemo)
    {
        I::SetNextWindowPos(ImVec2(500, 0), ImGuiCond_FirstUseEver);
        I::ShowDemoWindow(&G::Config.ShowImGuiDemo);
    }

    G::Windows::Notify(&Windows::Window::Update);
    if (G::Windows::Settings.Accepted)
        needInitialSettings = false;
    else if (needInitialSettings)
        return G::Windows::Settings.Show();

    if (scoped::MainMenuBar())
    {
        if (scoped::Menu("Config"))
        {
            if (I::MenuItem("Load"))
                G::Config.Load();
            if (I::MenuItem("Save"))
                G::Config.Save();
        }
        if (scoped::Menu("Viewers"))
        {
            using Viewer = Viewers::ViewerRegistry::RegisteredViewer;
            static auto viewers = []
            {
                std::vector<std::reference_wrapper<Viewer const>> viewers { std::from_range, Viewers::ViewerRegistry::GetRegistry() | std::views::filter(&Viewer::Constructor) };
                std::ranges::sort(viewers, [](Viewer const& a, Viewer const& b) { return std::string_view(a.Info.Name) < b.Info.Name; });
                return viewers;
            }();
            for (Viewer const& viewer : viewers)
            {
                if (I::MenuItem(std::format("<c=#8>New</c> {} <c=#8>Viewer</c>", viewer.Info.Title).c_str()))
                {
                    if (viewer.Info.Category == Viewers::Category::ListViewer)
                        m_listViewers.emplace_back(viewer.Constructor(m_nextViewerID++, false));
                    else
                        AddViewer(viewer.Constructor(m_nextViewerID++, false));
                }
            }
        }
        if (scoped::Menu("View"))
        {
            I::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
            if (I::MenuItem("Show Original Names", nullptr, &G::Config.ShowOriginalNames))
                G::Viewers::Notify(&Viewers::ContentListViewer::ClearCache);
            I::MenuItem("Show <c=#CCF>Valid Raw Pointers</c>", nullptr, &G::Config.ShowValidRawPointers);
            I::MenuItem("Show Content Symbol <c=#8>Name</c> Before <c=#4>Type</c>", nullptr, &G::Config.ShowContentSymbolNameBeforeType);
            I::MenuItem("Display Content Layout As  " ICON_FA_FOLDER_TREE " Tree", nullptr, &G::Config.TreeContentStructLayout);
            I::MenuItem("Open ImGui Demo Window", nullptr, &G::Config.ShowImGuiDemo);
            I::MenuItem("Open Parse Window", nullptr, &G::Windows::Parse.GetShown());
            I::MenuItem("Open Demangle Window", nullptr, &G::Windows::Demangle.GetShown());
            I::MenuItem("Open Notes Window", nullptr, &G::Windows::Notes.GetShown());
            I::MenuItem("Open Settings Window", nullptr, &G::Windows::Settings.GetShown());
            I::PopItemFlag();
        }
        if (scoped::Menu("Language"))
        {
            I::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
            for (auto const lang : magic_enum::enum_values<Language>())
            {
                if (I::MenuItem(magic_enum::enum_name(lang).data(), nullptr, G::Config.Language == lang))
                {
                    G::Config.Language = lang;
                    if (!G::Game.Text.IsLoaded(lang))
                    {
                        m_progress[3].Run([lang](Utils::Async::ProgressBarContext& progress)
                        {
                            G::Game.Text.LoadLanguage(lang, *G::Game.Archive.GetSource(), progress);
                        });
                    }
                }
            }
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
                    I::ProgressBar(-I::GetTime(), { 100, 16 });
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

    auto drawViewers = [this](std::list<std::unique_ptr<Viewers::Viewer>>& viewers, ImGuiID defaultDock)
    {
        std::unique_ptr<Viewers::Viewer> const* toRemove = nullptr;
        ImGuiWindow* focusWindow = nullptr;
        for (auto& viewer : viewers)
        {
            bool open = true;
            ImGuiID dock = defaultDock;
            if (defaultDock == center)
            {
                if (defaultDock == center && m_currentViewer && m_currentViewer->ImGuiWindow && m_currentViewer->ImGuiWindow->DockNode)
                    dock = m_currentViewer->ImGuiWindow->DockNode->ID;
            }
            I::SetNextWindowDockID(dock, ImGuiCond_Once);
            I::SetNextWindowClass(&windowClassViewer);
            if (scoped::Window(std::format("{}###Viewer-{}", viewer->Title(), viewer->ID).c_str(), &open, ImGuiWindowFlags_NoFocusOnAppearing))
            {
                viewer->ImGuiWindow = I::GetCurrentWindow();
                if (viewer->SetAfterCurrent && I::GetWindowDockNode())
                    if (m_currentViewer && m_currentViewer->ImGuiWindow && m_currentViewer->ImGuiWindow->DockNode == I::GetWindowDockNode())
                        if (auto tabBar = I::GetWindowDockNode()->TabBar)
                            if (auto currentViewerTab = I::TabBarFindTabByID(tabBar, I::GetWindowDockNode()->SelectedTabId))
                                if (auto viewerTab = I::TabBarFindTabByID(tabBar, viewer->ImGuiWindow->TabId))
                                    if (int offset = I::TabBarGetTabOrder(tabBar, currentViewerTab) - I::TabBarGetTabOrder(tabBar, viewerTab) + 1)
                                        I::TabBarQueueReorder(tabBar, viewerTab, offset);

                if (open && defaultDock == center && (!m_currentViewer || I::IsWindowFocused()))
                    m_currentViewer = viewer.get();

                viewer->Draw();
            }
            if (!open)
                toRemove = &viewer;
            else if (viewer->SetSelected)
                focusWindow = viewer->ImGuiWindow;

            viewer->SetSelected = false;
            viewer->SetAfterCurrent = false;

        }
        if (toRemove)
        {
            if (m_currentViewer == toRemove->get())
                m_currentViewer = nullptr;

            viewers.erase(std::ranges::find(viewers, *toRemove));
        }
        if (focusWindow)
        {
            auto old = I::GetCurrentContext()->NavWindow;
            I::FocusWindow(focusWindow);
            if (old)
                I::FocusWindow(old);
        }
    };
    drawViewers(m_listViewers, left);
    drawViewers(m_viewers, center);

    G::Game.Texture.UploadToGPU();
    static bool firstTime = [&]
    {
        if (!G::Config.GameDatPath.empty())
            G::Game.Archive.Add(Data::Archive::Kind::Game, G::Config.GameDatPath);
        if (!G::Config.LocalDatPath.empty())
            G::Game.Archive.Add(Data::Archive::Kind::Local, G::Config.LocalDatPath);
        m_progress[0].Run([this](Utils::Async::ProgressBarContext& progress)
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
            G::Viewers::Notify(&Viewers::FileListViewer::UpdateFilter);

            // Wait for PackFile layouts to load before continuing
            while (!G::Game.Pack.IsLoaded())
                std::this_thread::sleep_for(50ms);

            // Can't parallelize currently, Archive supports only single-thread file loading

            G::Game.Text.Load(source, progress);
            progress.Start("Creating string list");
            G::Viewers::Notify(&Viewers::StringListViewer::UpdateFilter);
            progress.Start("Creating conversation list");
            G::Viewers::Notify(&Viewers::ConversationListViewer::UpdateSearch);
            progress.Start("Creating event list");
            G::Viewers::Notify(&Viewers::EventListViewer::UpdateFilter);

            G::Game.Voice.Load(source, progress);
            m_progress[2].Run([&source](Utils::Async::ProgressBarContext& progress)
            {
                G::Game.Content.Load(source, progress);
                G::Viewers::Notify(&Viewers::ContentListViewer::UpdateFilter, false);

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
        m_progress[1].Run([](Utils::Async::ProgressBarContext& progress)
        {
            if (!G::Config.GameExePath.empty())
                G::Game.Pack.Load(G::Config.GameExePath, progress);
        });
        return true;
    }();
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
