module;
#include "UI/ImGui/ImGui.h"
#include "Utils/Utils.Async.h"

export module GW2Viewer.UI.Viewers.StringListViewer;
import GW2Viewer.Common;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Viewers.ListViewer;
import GW2Viewer.UI.Windows.ContentSearch;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Encoding;
import GW2Viewer.Utils.Format;
import GW2Viewer.Utils.Scan;
import GW2Viewer.Utils.String;
import std;
import <ctype.h>;

using namespace std::chrono_literals;

export namespace UI::Viewers
{

struct StringListViewer : ListViewer<StringListViewer>
{
    StringListViewer(uint32 id, bool newTab) : ListViewer(id, newTab) { }

    std::shared_mutex Lock;
    std::vector<uint32> FilteredList;
    Utils::Async::Scheduler AsyncFilter { true };

    std::string FilterString;
    std::optional<std::pair<int32, int32>> FilterID;
    uint32 FilterRange { };
    struct { bool Unencrypted = true, Encrypted = true, Decrypted = true, Empty = true; } Filters;
    enum class StringSort { ID, Text, DecryptionTime, Voice } Sort { StringSort::ID };
    bool SortInvert { };

    void SortList(Utils::Async::Context context, std::vector<uint32>& data, StringSort sort, bool invert)
    {
        #define COMPARE(a, b) do { if (auto const result = (a) <=> (b); result != std::strong_ordering::equal) return result == std::strong_ordering::less; } while (false)
        switch (sort)
        {
            using enum StringSort;
            case ID:
                std::ranges::sort(data, [invert](auto a, auto b) { return a < b ^ invert; });
                break;
            case Text:
                ComplexSort(data, invert, [](uint32 id)
                {
                    auto [string, status] = G::Game.Text.Get(id);
                    return string ? *string : L"";
                });
                break;
            case DecryptionTime:
                ComplexSort(data, invert, [](uint32 id) { return G::Game.Encryption.GetTextKeyInfo(id); }, [](uint32 a, uint32 b, auto const& aInfo, auto const& bInfo)
                {
                    if (aInfo && bInfo)
                    {
                        COMPARE(aInfo->Time, bInfo->Time);
                        COMPARE(aInfo->Index, bInfo->Index);
                    }
                    else
                        COMPARE((bool)aInfo, (bool)bInfo);
                    COMPARE(a, b);
                    return false;
                });
                break;
            case Voice:
                ComplexSort(data, invert, [](uint32 id) -> uint32
                {
                    if (auto const variants = G::Game.Text.GetVariants(id))
                        return variants->front();
                    if (auto const voice = G::Game.Text.GetVoice(id))
                        return voice;
                    return { };
                });
                break;
            default: std::terminate();
        }
        #undef COMPARE
    }
    void SetResult(Utils::Async::Context context, std::vector<uint32>&& data)
    {
        if (context->Cancelled) return;
        std::unique_lock _(Lock);
        FilteredList = std::move(data);
        context->Finish();
    }
    void UpdateSort()
    {
        AsyncFilter.Run([this, sort = Sort, invert = SortInvert](Utils::Async::Context context)
        {
            context->SetIndeterminate();
            std::vector<uint32> data;
            {
                std::shared_lock _(Lock);
                data = FilteredList;
            }
            CHECK_ASYNC;
            SortList(context, data, sort, invert);
            SetResult(context, std::move(data));
        });
    }
    void UpdateSearch()
    {
        bool textSearch = false;
        FilterID.reset();
        if (FilterString.empty())
            ;
        else if (uint32 id, range; Utils::Scan::Into(FilterString, "{}+{}", id, range))
            FilterID.emplace(id - range, id + range);
        else if (Utils::Scan::Into(FilterString, "{}-{}", id, range))
            FilterID.emplace(id, range);
        else if (Utils::Scan::NumberLiteral(FilterString, id))
            FilterID.emplace(id - FilterRange, id + FilterRange);
        else if (Utils::Scan::NumberLiteral(FilterString, "0x{:x}", id))
            FilterID.emplace(id - FilterRange, id + FilterRange);
        else
            textSearch = true;

        AsyncFilter.Run([this, textSearch, filter = FilterID, string = FilterString, filters = Filters, sort = Sort, invert = SortInvert](Utils::Async::Context context) mutable
        {
            context->SetIndeterminate();
            auto limits = filter.value_or(std::pair { std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() });
            limits.first = std::max(0, limits.first);
            limits.second = std::min((int32)G::Game.Text.GetMaxID() - 1, limits.second);
            std::vector<uint32> data;
            data.resize(limits.second - limits.first + 1);
            std::ranges::iota(data, limits.first);
            CHECK_ASYNC;
            if (!(filters.Unencrypted && filters.Encrypted && filters.Decrypted && filters.Empty))
            {
                std::erase_if(data, [&filters](uint32 id)
                {
                    auto [string, status] = G::Game.Text.Get(id);
                    if (!filters.Empty && string && (string->empty() || *string == L"[null]"))
                        return true;
                    switch (status)
                    {
                        using enum Data::Encryption::Status;
                        case Unencrypted: return !filters.Unencrypted;
                        case Encrypted: return !filters.Encrypted;
                        case Decrypted: return !filters.Decrypted;
                        case Missing: default: std::terminate();
                    }
                });
            }
            CHECK_ASYNC;
            if (textSearch)
            {
                static std::mutex parallelLock;
                std::unique_lock _(parallelLock);
                static std::unordered_map<std::thread::id, std::vector<uint32>> parallelResults;
                static std::mutex lock;
                std::ranges::for_each(parallelResults | std::views::values, &std::vector<uint32>::clear);
                std::wstring const query(std::from_range, Utils::Encoding::FromUTF8(string) | std::views::transform(toupper));
                std::for_each(std::execution::par_unseq, data.begin(), data.end(), [&query](uint32 stringID)
                {
                    thread_local auto& results = []() -> auto&
                    {
                        std::scoped_lock _(lock);
                        auto& container = parallelResults[std::this_thread::get_id()];
                        container.reserve(10000);
                        return container;
                    }();
                    if (auto const& [string, status] = G::Game.Text.GetNormalized(stringID); string && !string->empty() && string->contains(query))
                        results.emplace_back(stringID);
                });
                CHECK_ASYNC;
                data.assign_range(std::views::join(parallelResults | std::views::values));
            }
            CHECK_ASYNC;
            SortList(context, data, sort, invert);
            SetResult(context, std::move(data));
        });
    }
    auto UpdateFilter() { UpdateSearch(); }

    std::string Title() override { return ICON_FA_TEXT " Strings"; }
    void Draw() override
    {
        I::SetNextItemWidth(-(I::GetStyle().ItemSpacing.x + I::GetFrameHeight() + I::GetFrameHeight() + I::GetStyle().ItemSpacing.x + 60));
        if (static bool focus = true; std::exchange(focus, false))
            I::SetKeyboardFocusHere();
        if (I::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search...", &FilterString))
            UpdateSearch();
        if (auto context = AsyncFilter.Current())
        {
            I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
            if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
            if (scoped::WithColorVar(ImGuiCol_Border, 0))
            if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
            if (scoped::WithColorVar(ImGuiCol_Text, 0))
            if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                if (context.IsIndeterminate())
                    I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                else
                    I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
        }
        I::SameLine();
        auto now = G::UI.GetTime();
        static bool trackClipboard = false;
        static auto trackClipboardCooldown = now;
        static std::string previousClipboardContents;
        if (I::CheckboxButton(ICON_FA_CLIPBOARD, trackClipboard, "Track Clipboard", I::GetFrameHeight()) && trackClipboard)
            previousClipboardContents = I::GetClipboardText();
        if (trackClipboard && now >= trackClipboardCooldown)
        {
            trackClipboardCooldown = now + 100ms;
            if (auto clipboard = I::GetClipboardText(); clipboard && previousClipboardContents != clipboard)
            {
                FilterString = previousClipboardContents = clipboard;
                UpdateSearch();
            }
        }
        I::SameLine(0, 0);
        static bool copySingleResult = false;
        std::string singleResult;
        if (std::shared_lock __(Lock); FilteredList.size() == 1)
            if (auto [string, status] = G::Game.Text.Get(FilteredList.front()); string)
                singleResult = Utils::Encoding::ToUTF8(*string);
        static std::string previousSingleResult;
        if (I::CheckboxButton(ICON_FA_COPY, copySingleResult, "Auto Copy Single Result", I::GetFrameHeight()) && copySingleResult && !singleResult.empty())
            previousSingleResult = singleResult;
        if (copySingleResult && !singleResult.empty() && previousSingleResult != singleResult)
            I::SetClipboardText((previousClipboardContents = previousSingleResult = singleResult).c_str());
        I::SameLine();
        if (scoped::Disabled(!FilterID))
        {
            I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
            if (I::SetNextItemWidth(-FLT_MIN); I::DragInt("##SearchRange", (int*)&FilterRange, 0.1f, 0, 10000))
                UpdateSearch();
            if (I::IsItemHovered())
                I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        auto filter = [&, next = false](std::string_view text, bool& filter) mutable
        {
            if (std::exchange(next, true))
                I::SameLine();
            if (I::Button(std::format("<c=#{}><c=#8>{}</c> {}</c>###StringFilter{}", filter ? "F" : "4", ICON_FA_FILTER, text, text).c_str()))
            {
                filter ^= true;
                UpdateSearch();
            }
        };
        filter("Unencrypted", Filters.Unencrypted);
        filter("Encrypted", Filters.Encrypted);
        filter("Decrypted", Filters.Decrypted);
        filter("Empty", Filters.Empty);

        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, I::GetStyle().FramePadding))
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, I::GetStyle().ItemSpacing / 2))
        if (scoped::Table("Table", 4, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable))
        {
            I::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50, (ImGuiID)StringSort::ID);
            I::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch, 0, (ImGuiID)StringSort::Text);
            I::TableSetupColumn("Decrypted", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80, (ImGuiID)StringSort::DecryptionTime);
            I::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80, (ImGuiID)StringSort::Voice);
            I::TableSetupScrollFreeze(0, 1);
            I::TableHeadersRow();

            if (auto specs = I::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0)
            {
                Sort = (StringSort)specs->Specs[0].ColumnUserID;
                SortInvert = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                specs->SpecsDirty = false;
                UpdateSearch();
            }

            std::shared_lock __(Lock);
            ImGuiListClipper clipper;
            clipper.Begin(FilteredList.size());
            while (clipper.Step())
            {
                for (auto stringID : std::span(FilteredList.begin() + clipper.DisplayStart, FilteredList.begin() + clipper.DisplayEnd))
                {
                    scoped::WithID(stringID);

                    auto info = G::Game.Encryption.GetTextKeyInfo(stringID);
                    auto [string, status] = G::Game.Text.Get(stringID);
                    I::TableNextRow();
                    I::TableNextColumn();
                    I::SetNextItemAllowOverlap();
                    I::Selectable(std::format("{}", stringID).c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                    if (scoped::PopupContextItem())
                    {
                        static uint64 decryptionKey = 0;
                        I::Text("Text: %s%s", GetStatusText(status), string ? Utils::Encoding::ToUTF8(*string).c_str() : "");

                        Controls::CopyButton("ID", stringID);
                        I::SameLine();
                        Controls::CopyButton("DataLink", G::UI.MakeDataLink(0x03, 0x100 + stringID));
                        I::SameLine();
                        Controls::CopyButton("Text", string ? *string : L"", string);

                        if (I::InputScalar("Decryption Key", ImGuiDataType_U64, info ? &info->Key : &decryptionKey))
                        {
                            if (!info)
                                info = G::Game.Encryption.AddTextKeyInfo(stringID, { .Key = std::exchange(decryptionKey, 0) });
                            G::Game.Text.WipeCache(stringID);
                        }

                        if (I::Button("Search for Content References"))
                            G::Windows::ContentSearch.SearchForSymbolValue("StringID", stringID);
                    }

                    I::TableNextColumn();
                    std::string text = string ? Utils::Encoding::ToUTF8(*string).c_str() : "";
                    Utils::String::ReplaceAll(text, "\r", R"(<c=#F00>\r</c>)");
                    Utils::String::ReplaceAll(text, "\n", R"(<c=#F00>\n</c>)");
                    I::Text("%s%s", GetStatusText(status), text.c_str());

                    I::TableNextColumn();
                    if (info)
                    {
                        if (I::Selectable(std::format("<c=#{}>{}</c> {}###DecryptionTime", info->Map ? "F" : "2", ICON_FA_GLOBE, Utils::Format::DurationShortColored("{} ago", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - std::chrono::system_clock::from_time_t(info->Time)))).c_str()))
                        {
                            // TODO: Open map to { info->Map, info->Position }
                        }
                        if (scoped::ItemTooltip())
                            I::TextUnformatted(std::format("Decrypted on: {:%F %T}", std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(std::chrono::system_clock::from_time_t(info->Time)))).c_str());
                    }

                    I::TableNextColumn();
                    Controls::TextVoiceButton(stringID, { .Selectable = true });
                }
            }
        }
    }
};

}
