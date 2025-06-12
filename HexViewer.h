#pragma once
#include "Common.h"

#include <imgui.h>

#include <map>

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
void DrawHexViewer(std::span<byte const> data, HexViewerOptions& options);
