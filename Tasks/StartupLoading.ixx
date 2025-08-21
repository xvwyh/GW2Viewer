export module GW2Viewer.Tasks.StartupLoading;
import GW2Viewer.Common;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.External.Database;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Notifications;
import GW2Viewer.UI.Viewers.ContentListViewer;
import GW2Viewer.UI.Viewers.ConversationListViewer;
import GW2Viewer.UI.Viewers.EventListViewer;
import GW2Viewer.UI.Viewers.FileListViewer;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Viewers.StringListViewer;
import GW2Viewer.UI.Windows.MigrateContentTypes;
import GW2Viewer.User.ArchiveIndex;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Async.ProgressBarContext;
import std;
import magic_enum;
#include "Macros.h"

export namespace GW2Viewer::Tasks
{

struct StartupLoading
{
    enum class Tag
    {
        Archive,
        ArchiveIndex,
        Config,
        Content,
        Conversation,
        Encryption,
        Event,
        GameBuild,
        GameRefs,
        Manifest,
        PackFileLayout,
        Text,
        TextLanguage,
        Voice,
    };
    void Initialize()
    {
        m_initialized = true;

        using enum Tag;
        using Utils::Async::ProgressBarContext;
        AddTask({
            .Description = "Loading config",
            .Provides = { Config },
            .Handler = [](ProgressBarContext& progress)
            {
            }
        });
        AddTask({
            .Description = "Loading game",
            .Requires = { Config },
            .Provides = { GameBuild, GameRefs },
            .Handler = [](ProgressBarContext& progress)
            {
                if (!G::Config.GameExePath.empty())
                    G::Game.Load(G::Config.GameExePath, progress);
            }
        });
        AddTask({
            .Description = "Loading pack file definitions",
            .Requires = { Config },
            .Provides = { PackFileLayout },
            .Handler = [](ProgressBarContext& progress)
            {
                if (!G::Config.GameExePath.empty())
                    G::Game.Pack.Load(G::Config.GameExePath, progress);
            }
        });
        AddTask({
            .Description = "Loading external database",
            .Requires = { Config },
            .Provides = { Encryption, Conversation, Event },
            .Handler = [](ProgressBarContext& progress)
            {
                if (!G::Config.DecryptionKeysPath.empty())
                    G::Database.Load(G::Config.DecryptionKeysPath, progress);
            }
        });
        AddTask({
            .Description = "Loading archives",
            .Requires = { Config },
            .Provides = { Archive },
            .Handler = [](ProgressBarContext& progress)
            {
                if (!G::Config.GameDatPath.empty())
                    G::Game.Archive.Add(Data::Archive::Kind::Game, G::Config.GameDatPath);
                if (!G::Config.LocalDatPath.empty())
                    G::Game.Archive.Add(Data::Archive::Kind::Local,G::Config.LocalDatPath);
                G::Game.Archive.Load(progress);
            }
        });
        AddTask({
            .Description = "Loading archive index",
            .Requires = { Archive, GameBuild },
            .Provides = { ArchiveIndex },
            .Handler = [](ProgressBarContext& progress)
            {
                for (auto const [kind, name] : magic_enum::enum_entries<Data::Archive::Kind>())
                    if (auto const source = G::Game.Archive.GetSource(kind))
                        G::ArchiveIndex[kind].Load(*source, std::format("ArchiveIndex.{}.bin", name));
            }
        });
        AddTask({
            .Description = "Loading manifests",
            .Requires = { Archive, GameBuild, PackFileLayout },
            .Provides = { Manifest },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Game.Manifest.Load(progress);
            }
        });
        AddTask({
            .Description = "Loading text",
            .Requires = { Archive },
            .Provides = { Text },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Game.Text.Load(progress);
            }
        });
        for (auto const& language : magic_enum::enum_values<Language>())
        {
            AddTask({
                .Description = std::format("Loading {} text", language),
                .Requires = { Text },
                .Provides = { TextLanguage },
                .Condition = [language] { return G::Config.Language == language; },
                .Handler = [language](ProgressBarContext& progress)
                {
                    G::Game.Text.LoadLanguage(language, progress);
                }
            });
        }
        AddTask({
            .Description = "Loading voices",
            .Requires = { Archive },
            .Provides = { Voice },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Game.Voice.Load(progress);
            }
        });
        AddTask({
            .Description = "Loading content",
            .Requires = { Archive, Config },
            .Provides = { Content },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Game.Content.Load(progress);
                if (G::Game.Content.IsLoaded())
                {
                    progress.Start("Processing content types for migration");
                    if (!G::Config.LastNumContentTypes)
                        G::Config.LastNumContentTypes = G::Game.Content.GetNumTypes();
                    if (G::Config.LastNumContentTypes == G::Game.Content.GetNumTypes())
                    {
                        for (auto const type : G::Game.Content.GetTypes())
                            if (auto& typeInfo = type->GetTypeInfo(); typeInfo.Examples.empty() && !type->Objects.empty() && type->GUIDOffset >= 0)
                                typeInfo.Examples.insert_range(type->Objects | std::views::take(5) | std::views::transform([](Data::Content::ContentObject const* content) { return *content->GetGUID(); }));
                    }
                    else
                        G::Windows::MigrateContentTypes.Show();
                }
            }
        });
        AddTask({
            .Description = "Building file list",
            .Requires = { GameBuild, Archive, ArchiveIndex },
            .RerunOn = { GameRefs, Manifest },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Viewers::Notify(&UI::Viewers::FileListViewer::UpdateFilter);
            }
        });
        AddTask({
            .Description = "Building string list",
            .Requires = { Text },
            .RerunOn = { Encryption, TextLanguage },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Viewers::Notify(&UI::Viewers::StringListViewer::UpdateFilter);
            }
        });
        AddTask({
            .Description = "Building content list",
            .Requires = { Content },
            .RerunOn = { Encryption, TextLanguage },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Viewers::Notify(&UI::Viewers::ContentListViewer::UpdateFilter, false);
            }
        });
        AddTask({
            .Description = "Building conversation list",
            .Requires = { Conversation },
            .RerunOn = { Encryption, TextLanguage },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Viewers::Notify(&UI::Viewers::ConversationListViewer::UpdateSearch);
            }
        });
        AddTask({
            .Description = "Building event list",
            .Requires = { Event },
            .RerunOn = { Encryption, TextLanguage },
            .Handler = [](ProgressBarContext& progress)
            {
                G::Viewers::Notify(&UI::Viewers::EventListViewer::UpdateFilter);
            }
        });
    }

    struct Task
    {
        std::string Description;
        std::vector<Tag> Requires;
        std::vector<Tag> Provides;
        std::vector<Tag> RerunOn;
        std::function<bool()> Condition = [] { return true; };
        std::function<void(Utils::Async::ProgressBarContext& progress)> Handler;
    };

    bool IsLoaded(Tag tag) const { return m_providedTags[tag]; }

    void AddTask(Task task)
    {
        std::scoped_lock lock(m_mutex);
        m_scheduledTasks.emplace_back(std::move(task));
    }

    void Run()
    {
        if (!m_initialized)
            Initialize();

        std::scoped_lock lock(m_mutex);

        for (auto& task : m_runningTasks)
            if (task && !task->Progress.IsRunning())
                task.reset();

        for (auto& task : m_scheduledTasks)
            if (!task.Started && !task.Running && CanRunTask(task.Task))
                RunTask(task);
    }

private:
    bool m_initialized = false;
    std::mutex m_mutex;
    magic_enum::containers::array<Tag, bool> m_providedTags { };
    void ProvideTag(Tag tag)
    {
        std::scoped_lock lock(m_mutex);
        if (m_providedTags[tag])
            return;

        m_providedTags[tag] = true;
        for (auto& task : m_scheduledTasks)
            if (std::ranges::contains(task.Task.RerunOn, tag))
                task.Started = false;
    }

    struct ScheduledTask
    {
        Task Task;
        bool Started = false;
        bool Running = false;
    };
    std::list<ScheduledTask> m_scheduledTasks;

    struct RunningTask
    {
        ScheduledTask& ScheduledTask;
        Utils::Async::ProgressBarContext Progress;
    };
    static constexpr auto maxSimultaneousTasks = 10;
    std::array<std::unique_ptr<RunningTask>, maxSimultaneousTasks> m_runningTasks;

    bool CanRunTask(Task const& task) const
    {
        return std::ranges::all_of(task.Requires, std::bind_front(&StartupLoading::IsLoaded, this))
            && task.Condition()
            && std::ranges::any_of(m_runningTasks, [](auto& task) { return !task; });
    }
    void RunTask(ScheduledTask& task)
    {
        for (auto& run : m_runningTasks)
        {
            if (run)
                continue;

            task.Started = true;
            task.Running = true;
            run = std::make_unique<RunningTask>(task);
            if (!task.Task.Description.empty())
                run->Progress.Start(task.Task.Description);
            run->Progress.ShowNotification().Run([this, run = run.get()](Utils::Async::ProgressBarContext& progress)
            {
                run->ScheduledTask.Task.Handler(progress);
                for (auto const& tag : run->ScheduledTask.Task.Provides)
                    ProvideTag(tag);
                run->ScheduledTask.Running = false;
            });
            return;
        }
        std::terminate();
    }
    void CleanupRunningTasks()
    {
        for (auto& task : m_runningTasks)
            if (task && !task->Progress.IsRunning())
                task.reset();
    }
};

}

export namespace GW2Viewer::G::Tasks { GW2Viewer::Tasks::StartupLoading StartupLoading; }
