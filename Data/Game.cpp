module GW2Viewer.Data.Game;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Pack;
import GW2Viewer.Utils.ScanPE;

namespace GW2Viewer::Data
{

void Game::Load(std::filesystem::path const& path, Utils::Async::ProgressBarContext& progress)
{
    progress.Start(std::format("Parsing build info from {}", path.filename().string()));
    Utils::ScanPE::Scanner scanner { path };

    if (auto const branch = std::ranges::search(scanner.rdata, std::span((byte const*)L"Gw2\0", 8)); !branch.empty())
    {
        static auto skipPadding = [](byte const*& p) { while (*++p == 0xCC) { } };
        uint32 offset;
        auto const pOffset = (byte const*)&offset;
        auto const pOffsetEnd = pOffset + sizeof(offset);
        for (byte const* p = scanner.text.begin(); p != scanner.text.end() - sizeof(offset); ++p)
            if (uint64 const longOffset = branch.data() - p - sizeof(offset); longOffset <= std::numeric_limits<decltype(offset)>::max())
                if (offset = (decltype(offset))longOffset; std::ranges::equal(p, p + sizeof(offset), pOffset, pOffsetEnd) && (p[-3] == 0x48 || p[-3] == 0x4C) && p[-2] == 0x8D)
                    if (*(p += 4) == 0xC3)
                        if (skipPadding(p), (p[0] == 0x48 || p[0] == 0x4C) && p[1] == 0x8D && *(p += 7) == 0xC3)
                            if (skipPadding(p), p[0] == 0xB8)
                                if (uint32 const build = *(uint32 const*)&p[1]; build < 300000 && ((Build = build)))
                                    break;
    }

    progress.Start("Searching for embedded filenames", scanner.text.size());
    while (!G::Game.Archive.IsLoaded())
        std::this_thread::sleep_for(50ms);

    for (byte const* p = scanner.text.begin(); p != scanner.text.end() - 7; ++p)
    {
        if ((p[0] == 0x48 || p[0] == 0x4C) && p[1] == 0x8D)
        {
            for (auto target = p + 7 + *(uint32 const*)&p[3]; (scanner.rdata.Contains(target) || scanner.data.Contains(target)) && !target[6] && !target[7]; target += 8)
            {
                if (uint32 const fileID = ((Pack::FileReference const*)target)->GetFileID(); fileID && fileID <= G::Game.Archive.GetMaxFileID())
                    ReferencedFiles.emplace(fileID);
                else
                    break;
            }
            if (auto indirection = (byte const* const*)(p + 7 + *(uint32 const*)&p[3]); scanner.rdata.Contains(indirection) || scanner.data.Contains(indirection))
            {
                for (auto target = *indirection; (scanner.rdata.Contains(target) || scanner.data.Contains(target)) && !target[6] && !target[7]; target += 8)
                {
                    if (uint32 const fileID = ((Pack::FileReference const*)target)->GetFileID(); fileID && fileID <= G::Game.Archive.GetMaxFileID())
                        ReferencedFiles.emplace(fileID);
                    else
                        break;
                }
            }
        }

        if (static constexpr uint32 interval = 1000; !(std::distance(scanner.text.begin(), p) % interval))
            progress += interval;
    }
}

}
