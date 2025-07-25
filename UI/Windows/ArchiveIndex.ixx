module;
#include "UI/ImGui/ImGui.h"
#include "Utils/Async.h"

export module GW2Viewer.UI.Windows.ArchiveIndex;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Archive;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.User.ArchiveIndex;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Container;
import std;
import magic_enum;

export namespace GW2Viewer::UI::Windows
{

struct ArchiveIndex : Window
{
    struct Archive
    {
        char const* Name;
        User::ArchiveIndex& Index;
        User::ArchiveIndex::ScanOptions ScanOptions;
        User::ArchiveIndex::ScanProgress ScanProgress;
        User::ArchiveIndex::ScanResult ScanResult;
        bool ScanInProgress = false;
        std::unordered_map<char const*, Utils::Async::Scheduler> AsyncExport;

        std::string Log;
        bool WriteLog = false;
        bool WritingLog = false;

        void Draw()
        {
            scoped::WithID(this);
            if (!Index.IsLoaded())
            {
                I::Text("Specify the path to %s in settings and restart the application.", Name);
                return;
            }

            auto context = Index.AsyncScan.Current();
            ScanInProgress = context;

            if (scoped::Disabled(ScanInProgress))
            {
                I::Checkbox("Update files that couldn't be identified (Unknown)", &ScanOptions.UpdateUnknown);
                I::Checkbox("Update files that had an error during identification (Error)", &ScanOptions.UpdateError);
                I::Checkbox("Update files that weren't identified (Uncategorized)", &ScanOptions.UpdateUncategorized);
                I::Checkbox("Rescan files that were identified", &ScanOptions.UpdateCategorized);
            }

            if (ScanInProgress)
            {
                if (I::Button("Stop"))
                    Index.StopScan();

                I::SameLine();
                I::SetNextItemWidth(-FLT_MIN);
                if (scoped::Disabled(true))
                    I::InputText("##Description", (char*)std::format("{} / {}", context.Current, context.Total).c_str(), 9999);
                Controls::AsyncProgressBar(Index.AsyncScan);
            }
            else
            {
                if (scoped::Disabled(std::ranges::any_of(AsyncExport | std::views::values, &Utils::Async::Scheduler::Current)))
                {
                    if (I::Button("Start Full Scan"))
                    {
                        ScanProgress = { };
                        Index.ScanAsync(ScanOptions, ScanProgress, [this](auto const& result)
                        {
                            ScanResult = result;
                            Log = std::format("Updated: {} Scanned: {} Total: {}\n", result.Updated, result.Scanned, result.Total);
                            WriteLog = true;
                        });
                    }
                }
            }

            if (std::exchange(WriteLog, false))
                WritingLog = true;

            I::Separator();
            DrawFileSummary("Added Files",             "0F0", &User::ArchiveIndex::ScanProgress::NewFiles,        &User::ArchiveIndex::ScanResult::NewFiles);
            DrawFileSummary("Changed Files",           "FF0", &User::ArchiveIndex::ScanProgress::ChangedFiles,    &User::ArchiveIndex::ScanResult::ChangedFiles);
            DrawFileSummary("Deleted Files",           "F00", &User::ArchiveIndex::ScanProgress::DeletedFiles,    &User::ArchiveIndex::ScanResult::DeletedFiles);
            I::Spacing();
            DrawFileSummary("Failed to Scan Files",    "F00", &User::ArchiveIndex::ScanProgress::ErrorFiles,      &User::ArchiveIndex::ScanResult::ErrorFiles);
            DrawFileSummary("Corrupted Index Entries", "F00", &User::ArchiveIndex::ScanProgress::CorruptedCaches, &User::ArchiveIndex::ScanResult::CorruptedCaches);

            if (std::exchange(WritingLog, false))
            {
                std::filesystem::path path = std::format(R"(Export\Index\{:%F_%H-%M-%S}Z_{}_{}.log)", Time::FromTimestamp(Index.GetArchiveTimestamp()), G::Game.Build, Name);
                create_directories(path.parent_path());
                auto originalPath = path;
                int attempt = 1;
                while (exists(path))
                    path.replace_filename(originalPath.stem().string() + std::format(" ({})", attempt++) + originalPath.extension().string());
                G::UI.ExportData({ (byte const*)Log.data(), Log.size() }, path);
            }
        }

        void DrawFileSummary(char const* header, char const* color, auto (User::ArchiveIndex::ScanProgress::* progressField), auto (User::ArchiveIndex::ScanResult::* resultField))
        {
            scoped::WithID(header);

            auto& results = ScanResult.*resultField;
            static constexpr bool isMap = Utils::Container::MapContainer<decltype(results)>;

            uint32 count = ScanInProgress ? ScanProgress.*progressField : (uint32)results.size();
            I::AlignTextToFramePadding();
            I::Text("%s: <c=#%s>%u</c>", header, count ? color : "4", count);
            if (!ScanInProgress && count)
            {
                I::SameLine();
                if (I::Button(ICON_FA_COPY " File List") || WritingLog)
                {
                    uint32 elementSize = 10;
                    if constexpr (isMap)
                        elementSize += 100;
                    std::string buffer;
                    buffer.reserve(elementSize * results.size());
                    auto out = std::back_inserter(buffer);

                    static auto printableFourCC = [editedFourCC = 0u](uint32 fourCC) mutable
                    {
                        editedFourCC = fourCC;
                        for (auto& c : std::span((char*)&editedFourCC, 4))
                            if (!isprint(c) && !isspace(c))
                                c = '?';
                        return std::string_view((char const*)&editedFourCC, 4);
                    };
                    if constexpr (isMap)
                    {
                        for (auto const& [fileID, cache] : results)
                            std::format_to(out, "{} (Was {})\n", fileID, Index.GetMetadata(cache.MetadataIndex).ToString());
                    }
                    else
                    {
                        for (auto const fileID : results)
                            std::format_to(out, "{}\n", fileID);
                    }

                    if (WritingLog)
                    {
                        Log += std::format("\n{}:\n", header);
                        Log += buffer;
                    }
                    else
                        I::SetClipboardText(buffer.data());
                }
                I::SameLine();
                auto& async = AsyncExport[header];
                if (auto context = async.Current())
                {
                    if (I::Button(ICON_FA_FLOPPY_DISK " Stop"))
                        async.Run([](Utils::Async::Context context) { context->Finish(); });

                    I::SameLine();
                    I::SetNextItemWidth(-FLT_MIN);
                    if (scoped::Disabled(true))
                        I::InputText("##Description", (char*)std::format("{} / {}", context.Current, context.Total).c_str(), 9999);
                    Controls::AsyncProgressBar(async);
                }
                else if (I::Button(ICON_FA_FLOPPY_DISK " Export"))
                {
                    async.Run([this, &results](Utils::Async::Context context)
                    {
                        std::vector<uint32> files;
                        if constexpr (isMap)
                            files.assign_range(results | std::views::keys);
                        else
                            files.assign_range(results);

                        context->SetTotal(files.size());
                        for (auto fileID : files)
                        {
                            CHECK_ASYNC;
                            auto data = Index.GetSource().Archive.GetFile(fileID);
                            std::filesystem::path path = std::format(R"(Export\Index\{:%F_%H-%M-%S}Z_{}_{}\{})", Time::FromTimestamp(Index.GetArchiveTimestamp()), G::Game.Build, Name, fileID);
                            create_directories(path.parent_path());
                            G::UI.ExportData(data, path);
                            //path.replace_extension(".png");
                            //G::Game.Texture.Load(fileID, { .DataSource = &data, .ExportPath = path });
                            context->Increment();
                        }

                        context->Finish();
                    });
                }
            }
        }
    } Archives[2]
    {
        { "Gw2.dat", G::ArchiveIndex[Data::Archive::Kind::Game] },
        { "Local.dat", G::ArchiveIndex[Data::Archive::Kind::Local] },
    };

    std::string Title() override { return "Archive Index"; }
    void Draw() override
    {
        if (scoped::TabBar("##Archives"))
            for (auto& archive : Archives)
                if (scoped::TabItem(archive.Name))
                    archive.Draw();
    }
};

}

export namespace GW2Viewer::G::Windows { UI::Windows::ArchiveIndex ArchiveIndex; }
