module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Controls:HexViewer;
import GW2Viewer.Common;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Pack;
import GW2Viewer.Utils.Encoding;
import std;

namespace GW2Viewer
{

struct ValueRepBase
{
    virtual ~ValueRepBase() { }
    virtual void Draw(byte const* dataPtr, uint32 available) = 0;
};
template<typename T>
struct ValueRepNumeric : ValueRepBase
{
    char const* Name;
    char const* Name2;
    char const* Format;

    ValueRepNumeric(char const* name, char const* name2, char const* format) : Name(name), Name2(name2), Format(format) { }

    void Draw(byte const* dataPtr, uint32 available) override
    {
        if (available < sizeof(T))
            return;

        I::TableNextRow();
        I::TableNextColumn(); I::TextUnformatted(Name);
        I::TableNextColumn(); I::TextUnformatted(Name2);
        I::TableNextColumn();
        if constexpr (std::is_same_v<T, wchar_t>)
            I::TextUnformatted(std::format("{}", Utils::Encoding::ToUTF8(std::wstring_view((T const*)dataPtr, 1))).c_str());
        else
            I::TextUnformatted(std::vformat(Format, std::make_format_args(*(T const*)dataPtr)).c_str());
    }
};
template<typename T>
struct ValueRepString : ValueRepBase
{
    char const* Name;

    ValueRepString(char const* name) : Name(name) { }

    void Draw(byte const* dataPtr, uint32 available) override
    {
        using CharT = std::remove_pointer_t<T>;
        if constexpr (std::is_pointer_v<T>)
        {
            if (available < sizeof(CharT const*))
                return;

            auto* ptr = *(CharT const**)dataPtr;
            if (!ptr)
                return;

            dataPtr = (byte const*)ptr;
            try { for (available = 0; *ptr && available <= 1024; ++ptr, ++available) { } }
            catch (std::exception const&) { return; }
            if (available > 1024)
                return;
            available *= sizeof(CharT);
        }

        auto* start = (CharT const*)dataPtr;
        auto* end = start;
        while (available >= sizeof(CharT) && *end)
        {
            ++end;
            available -= sizeof(CharT);
        }
        if ((std::make_signed_t<decltype(available)>)available < 0)
            return;

        auto chars = std::distance(start, end);
        bool ellipsis = false;
        if (chars > 20)
        {
            chars = 20;
            ellipsis = true;
        }

        std::string utf8;
        if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            utf8 = Utils::Encoding::ToUTF8(std::wstring_view(start, chars));
            start = (CharT const*)utf8.data();
            chars = utf8.size();
        }

        I::TableNextRow();
        I::TableNextColumn(); I::TextUnformatted(Name);
        I::TableNextColumn();
        I::TableNextColumn(); I::Text("%.*s", chars, (char*)start);
        if (ellipsis)
        {
            I::SameLine();
            I::TextColored({ 1, 1, 1, 0.5f }, "[...]");
        }
    }
};
struct ValueRepToken32 : ValueRepBase
{
    void Draw(byte const* dataPtr, uint32 available) override
    {
        if (available < sizeof(GW2Viewer::Token32))
            return;

        auto const& token = *(GW2Viewer::Token32 const*)dataPtr;
        if (token.empty())
            return;

        I::TableNextRow();
        I::TableNextColumn(); I::TextUnformatted("Token32");
        I::TableNextColumn();
        I::TableNextColumn(); I::TextUnformatted(token.GetString().data());
    }
};
struct ValueRepToken64 : ValueRepBase
{
    void Draw(byte const* dataPtr, uint32 available) override
    {
        if (available < sizeof(GW2Viewer::Token64))
            return;

        auto const& token = *(GW2Viewer::Token64 const*)dataPtr;
        if (token.empty())
            return;

        I::TableNextRow();
        I::TableNextColumn(); I::TextUnformatted("Token64");
        I::TableNextColumn();
        I::TableNextColumn(); I::TextUnformatted(token.GetString().data());
    }
};
struct ValueRepFileID : ValueRepBase
{
    void Draw(byte const* dataPtr, uint32 available) override
    {
        if (available < sizeof(uint32))
            return;

        auto const fileID = *(uint32 const*)dataPtr;
        if (!G::Game.Archive.ContainsFile(fileID))
            return;

        I::TableNextRow();
        I::TableNextColumn(); I::TextUnformatted("FileID");
        I::TableNextColumn(); I::Text("%u", fileID);
        I::TableNextColumn();
    }
};
struct ValueRepFileReference : ValueRepBase
{
    void Draw(byte const* dataPtr, uint32 available) override
    {
        if (available < sizeof(GW2Viewer::Data::Pack::FileReference))
            return;

        auto const fileID = ((GW2Viewer::Data::Pack::FileReference const*)dataPtr)->GetFileID();
        if (!G::Game.Archive.ContainsFile(fileID))
            return;

        I::TableNextRow();
        I::TableNextColumn(); I::TextUnformatted("FileID");
        I::TableNextColumn(); I::Text("%u", fileID);
        I::TableNextColumn();
    }
};
struct ValueRepStringID : ValueRepBase
{
    void Draw(byte const* dataPtr, uint32 available) override
    {
        if (available < sizeof(uint32))
            return;

        auto const stringID = *(uint32 const*)dataPtr;
        if (!stringID || stringID >= G::Game.Text.GetMaxID())
            return;

        auto [string, status] = G::Game.Text.Get(stringID);
        std::string const utf8 = string ? Utils::Encoding::ToUTF8(*string) : "";

        I::TableNextRow();
        I::TableNextColumn(); I::TextUnformatted("StringID");
        I::TableNextColumn(); I::Text("%u", stringID);
        I::TableNextColumn(); I::Text("%s%s", GetStatusText(status), utf8.c_str());
    }
};
struct ValueRepContentObject : ValueRepBase
{
    void Draw(byte const* dataPtr, uint32 available) override
    {
        if (available < sizeof(Data::Content::ContentObject*))
            return;

        auto const contentDataPtr = *(byte* const*)dataPtr;
        auto const object = G::Game.Content.GetByDataPointer(contentDataPtr);
        if (!object)
            return;
        object->Finalize();

        I::TableNextRow();
        I::TableNextColumn(); I::TextUnformatted("Content");
        I::TableNextColumn(); I::Text("%s", Utils::Encoding::ToUTF8(object->Type->GetDisplayName()).c_str());
        I::TableNextColumn(); I::Text("%s", Utils::Encoding::ToUTF8(object->GetFullDisplayName()).c_str());
    }
};

inline auto& GetValueReps()
{
    static ValueRepBase* instance[]
    {
        new ValueRepNumeric<   byte >(" uint8   ", "   byte  ", "{:d}"),
        new ValueRepNumeric<  sbyte >("  int8   ", "  sbyte  ", "{:d}"),
        new ValueRepNumeric<  uint16>(" uint16  ", "  uword  ", "{:d}"),
        new ValueRepNumeric<   int16>("  int16  ", "   word  ", "{:d}"),
        new ValueRepNumeric<  uint32>(" uint32  ", " udword> ", "{:d}"),
        new ValueRepNumeric<   int32>("  int32  ", "  dword> ", "{:d}"),
        new ValueRepNumeric<  uint64>(" uint64  ", " uqword  ", "{:d}"),
        new ValueRepNumeric<   int64>("  int64  ", "  qword  ", "{:d}"),
        new ValueRepNumeric<   float>("  float  ", "  float> ", "{:f}"),
      //new ValueRepNumeric<  double>(" double  ", " double  ", "{:f}"),
        new ValueRepNumeric<   byte >(" hex  8  ",          "", "{:X}"),
        new ValueRepNumeric<  uint16>(" hex 16  ",          "", "{:X}"),
        new ValueRepNumeric<  uint32>(" hex 32  ",          "", "{:X}"),
        new ValueRepNumeric<  uint64>(" hex 64  ",          "", "{:X}"),
        new ValueRepNumeric<   char >("   char  ", "   char  ", "{:c}"),
        new ValueRepNumeric<wchar_t >("  wchar  ", "  wchar  ", "{:c}"),
        new ValueRepString <   char >(" string  "),
        new ValueRepString <wchar_t >("wstring  "),
        new ValueRepString <   char*>(" string* "),
        new ValueRepString <wchar_t*>("wstring* "),
        new ValueRepToken32(),
        new ValueRepToken64(),
        new ValueRepFileID(),
        new ValueRepFileReference(),
        new ValueRepStringID(),
        new ValueRepContentObject(),
    };
    return instance;
}

}

export namespace GW2Viewer::UI::Controls
{

struct HexViewerCellInfo
{
    uint32 ByteOffset;
    ImVec2 CellCursor;
    ImVec2 CellSize;
    ImVec2 TableCursor;
    ImVec2 TableSize;
};
struct HexViewerOptions
{
    uint32 StartOffset = 0;
    bool ShowHeaderRow = false;
    bool ShowOffsetColumn = false;
    bool ShowVerticalScroll = true;
    bool FillWindow = false;
    std::optional<HexViewerCellInfo> OutHoveredInfo;
    std::optional<uint32> OutHighlightOffset;
    std::optional<byte const*> OutHighlightPointer;
    std::map<uint32, HexViewerCellInfo> OutOffsetInfo;
    byte const* ByteMap = nullptr;
};
void HexViewer(std::span<byte const> data, HexViewerOptions& options)
{
    static constexpr auto BYTE_MAP_COLORS = []
    {
        std::array<ImU32, std::numeric_limits<byte>::max() + 1> colors { };
        colors[0xAA] = 0x200000FF; // local offset
        colors[0xBB] = 0x2000FFFF; // string
        colors[0xEE] = 0x20FF0000; // external offset
        colors[0xFF] = 0x2000FF00; // file
        return colors;
    }();
    float offsetColumnWidth = options.ShowOffsetColumn ? 60 : 0;
    static constexpr ImVec2 BYTE_SIZE(20, 15);
    ImVec2 avail = I::GetContentRegionAvail();
    if (!options.FillWindow)
        avail.y = 100000;
    ImVec2 tableSize = avail - ImVec2(options.FillWindow && options.ShowVerticalScroll ? I::GetStyle().ScrollbarSize : 0, 0);
    ImVec2 const chars = (tableSize - ImVec2(offsetColumnWidth, 0)) / BYTE_SIZE;
    auto cols = std::max<int>(1, chars.x);
    auto rows = std::max<int>(1, chars.y);
    if (cols > 4)
        cols = cols / 4 * 4;
    if (!options.FillWindow)
    {
        cols = std::min<int>(cols, data.size());
        rows = std::min<int>(rows, (data.size() + cols - 1) / cols);
        tableSize = { offsetColumnWidth + BYTE_SIZE.x * cols, BYTE_SIZE.y * (rows + (options.ShowHeaderRow ? 1 : 0)) };
    }
    I::PushFont(I::GetIO().Fonts->Fonts[0]);
    auto tableCursor = I::GetCursorScreenPos();
    if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
    if (scoped::WithColorVar(ImGuiCol_FrameBg, 0xFF040404))
    if (scoped::WithColorVar(ImGuiCol_TableRowBgAlt, I::GetColorU32(ImGuiCol_TableRowBg)))
    if (scoped::Table("Hex", (options.ShowOffsetColumn ? 1 : 0) + cols, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_PreciseWidths | (options.FillWindow ? ImGuiTableFlags_ScrollY : 0)))
    {
        if (options.ShowOffsetColumn)
            I::TableSetupColumn("Offset +", ImGuiTableColumnFlags_WidthFixed, offsetColumnWidth);
        for (int col = 0; col < cols; ++col)
            I::TableSetupColumn(std::format("{:02X}", col).c_str(), ImGuiTableColumnFlags_WidthFixed, BYTE_SIZE.x);
        I::TableSetupScrollFreeze(options.ShowOffsetColumn ? 1 : 0, 1);
        if (options.ShowHeaderRow)
            I::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((data.size() + cols - (options.ShowOffsetColumn ? 1 : 0)) / cols, BYTE_SIZE.y);
        uint32 buttonID = 0;
        while (clipper.Step())
        {
            int col = -1;
            int row = 0;
            for (auto const& byte : data | std::views::drop(clipper.DisplayStart * cols) | std::views::take((clipper.DisplayEnd - clipper.DisplayStart) * cols))
            {
                if (++col == cols)
                {
                    col = 0;
                    ++row;
                }
                auto const rowOffset = clipper.DisplayStart * cols + row * cols;
                auto const byteOffset = rowOffset + col;
                auto const absoluteOffset = options.StartOffset + byteOffset;
                if (!col)
                {
                    I::TableNextRow(ImGuiTableRowFlags_None, BYTE_SIZE.y);
                    if (options.ShowOffsetColumn)
                    {
                        I::TableNextColumn();
                        std::string text = std::format("{:X} ", absoluteOffset);
                        auto const space = ImVec2(I::GetContentRegionAvail().x, BYTE_SIZE.y);
                        auto const size = I::CalcTextSize(text.c_str());
                        I::GetWindowDrawList()->AddRectFilled(I::GetCursorScreenPos(), I::GetCursorScreenPos() + space, I::GetColorU32(ImGuiCol_TableHeaderBg));
                        I::SetCursorPos(I::GetCursorPos() + ImVec2(space.x - size.x, (space.y - size.y) / 2));
                        I::TextColored({ 0.75f, 0.75f, 1, 1 }, text.c_str());
                    }
                }
                I::TableNextColumn();
                ImVec4 color { 1, 1, 1, byte ? 0.25f + byte / 255.0f * 0.75f : 0.1f };

                size_t numPrintableCharsAround = 0;
                if (data.size() >= 1)
                {
                    size_t from = std::max(0, byteOffset - 3);
                    size_t to = std::min(byteOffset + 4, (int)data.size() - 1);
                    numPrintableCharsAround = std::ranges::any_of(std::ranges::subrange(&data[from], std::unreachable_sentinel) | std::views::take(to - from) | std::views::slide(4), [](auto const& range)
                    {
                        return std::ranges::all_of(range, [](GW2Viewer::byte c) { return isprint(c) || isspace(c); });
                    }) ? 999 : 0;
                }

                size_t numPrintableWCharsAround = 0;
                if (data.size() >= 2)
                {
                    size_t from = std::max(0, byteOffset - 6);
                    size_t to = std::min(byteOffset + 8, (int)data.size() - 2);
                    numPrintableWCharsAround = std::ranges::any_of(std::ranges::subrange(&data[from], std::unreachable_sentinel) | std::views::take(to - from) | std::views::slide(8), [](auto const& range)
                    {
                        return std::ranges::all_of(range | std::views::chunk(2), [](auto const& range)
                        {
                            wchar_t const c = *(wchar_t*)&*std::begin(range);
                            return c && (iswascii(c) && isalnum(c)|| iswspace(c));
                        });
                    }) ? 999 : 0;
                }

                if (numPrintableWCharsAround >= 4)
                {
                    wchar_t wbyte = *(wchar_t*)&byte;
                    if (iswdigit(wbyte))
                        color = color * ImVec4(0.25f, 1.0f, 0.75f, 1);
                    else if (iswalnum(wbyte) || iswspace(wbyte))
                        color = color * ImVec4(0.25f, 0.5f, 1.0f, 1);
                    else
                        color = color * ImVec4(0.25f, 0.25f, 1.0f, 1);
                }
                else if (numPrintableCharsAround >= 4)
                {
                    if (isdigit(byte))
                        color = color * ImVec4(0.75f, 1.0f, 0.25f, 1);
                    else if (isalnum(byte) || isspace(byte))
                        color = color * ImVec4(1.0f, 0.5f, 0.25f, 1);
                    else
                        color = color * ImVec4(1.0f, 0.25f, 0.25f, 1);
                }

                auto const cursor = I::GetCursorScreenPos();
                if (scoped::WithColorVar(ImGuiCol_Button, 0))
                if (scoped::WithColorVar(ImGuiCol_Border, 0))
                if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
                if (scoped::WithColorVar(ImGuiCol_Text, color))
                if (scoped::WithStyleVar(ImGuiStyleVar_FrameRounding, 0))
                if (scoped::WithStyleVar(ImGuiStyleVar_FramePadding, ImVec2()))
                {
                    I::PushID(absoluteOffset);
                    I::Button(std::format("{:02X}###{}", byte, buttonID++).c_str(), BYTE_SIZE);
                    I::PopID();
                }
                if (I::IsItemHovered())
                {
                    options.OutHighlightOffset.reset();
                    options.OutHighlightPointer.reset();
                    if (data.size() - byteOffset >= sizeof(int32))
                        if (auto offset = *(int32 const*)&data[byteOffset])
                            options.OutHighlightOffset = absoluteOffset + offset;
                    if (data.size() - byteOffset >= sizeof(GW2Viewer::byte*))
                        if (auto ptr = *(GW2Viewer::byte* const*)&data[byteOffset])
                            options.OutHighlightPointer = ptr;
                    options.OutHoveredInfo = { absoluteOffset, cursor, BYTE_SIZE, tableCursor, tableSize };
                }

                if (options.OutHighlightOffset && *options.OutHighlightOffset == absoluteOffset)
                    I::GetForegroundDrawList()->AddLine(cursor, cursor + ImVec2(0, BYTE_SIZE.y), 0xFFFF0000, 2);
                if (options.OutHighlightPointer && *options.OutHighlightPointer == &data[byteOffset])
                    I::GetForegroundDrawList()->AddLine(cursor, cursor + ImVec2(0, BYTE_SIZE.y), 0xFF0000FF, 2);
                if (!options.OutOffsetInfo.empty())
                    if (auto itr = options.OutOffsetInfo.find(absoluteOffset); itr != options.OutOffsetInfo.end())
                        itr->second = { absoluteOffset, cursor, BYTE_SIZE, tableCursor, tableSize };

                if (options.ByteMap)
                    if (auto const color = BYTE_MAP_COLORS[options.ByteMap[absoluteOffset]])
                        I::GetWindowDrawList()->AddRectFilled(cursor, cursor + BYTE_SIZE, color);

                if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
                {
                    I::Text("Offset: 0x%X (%u)", absoluteOffset, absoluteOffset);
                    I::Text("Memory: 0x%llX", (uintptr_t)&data[byteOffset]);
                    I::Spacing();
                    if (scoped::Table("Values", 3, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInner))
                    {
                        I::TableSetupColumn("Type");
                        I::TableSetupColumn("Name");
                        I::TableSetupColumn("Value");
                        for (auto* rep : GetValueReps())
                            rep->Draw(&data[byteOffset], data.size() - byteOffset);
                    }
                }
            }
        }
    }

    for (uint32 i = 1; i < cols / 4; ++i)
        I::GetWindowDrawList()->AddLine(tableCursor + ImVec2(offsetColumnWidth + BYTE_SIZE.x * i * 4, 0), tableCursor + ImVec2(offsetColumnWidth + BYTE_SIZE.x * i * 4, tableSize.y), 0x10FFFFFF);

    I::PopFont();
}

}
