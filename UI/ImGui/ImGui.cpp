module;
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <shellapi.h>
#include <cassert>

module GW2Viewer.UI.ImGui;
import std;

using GW2Viewer::byte;

static constexpr auto cihash = [](std::string_view const& key)
{
    return std::hash<std::string>()({ std::from_range, key | std::views::transform(tolower) });
};
static constexpr auto cieq = [](std::string_view const& a, std::string_view const& b)
{
    return std::ranges::equal(a | std::views::transform(tolower), b | std::views::transform(tolower));
};
std::unordered_map<std::string_view, ImU32, decltype(cihash), decltype(cieq)> gw2colors
{
    { "AbilityType", 0xFF8CECFF },
    { "Flavor",      0xFFE4E89B },
    { "Reminder",    0xFFB0B0B0 },
    { "Quest",       0xFF00FF00 },
    { "Task",        0xFF57C9FF },
    { "Warning",     0xFF0200ED },
    { "Event",       0xFF3366CC },
};

enum class ColorChangeType : byte
{
    Unchanged,
    Push,
    PushAlpha,
    PushColor,
    Pop,
};
struct MarkupState
{
    ImU32 Color;
    ColorChangeType ColorChangeType = ColorChangeType::Unchanged;
    byte BoldDepth = 0;
    byte NoSelectDepth = 0;
    bool IsInNoSelect() const { return NoSelectDepth; }
};
bool ParseMarkup(const char*& s, size_t avail, MarkupState& state)
{
    if (s[0] != '<')
        return false;
    state.ColorChangeType = ColorChangeType::Unchanged;
    if (avail >= 3 && !strncmp(s, "<b>", 3))
    {
        assert(state.BoldDepth < std::numeric_limits<decltype(state.BoldDepth)>::max());
        ++state.BoldDepth;
        s += 3;
        return true;
    }
    if (avail >= 4 && !strncmp(s, "</b>", 4))
    {
        assert(state.BoldDepth);
        --state.BoldDepth;
        s += 4;
        return true;
    }
    if (avail >= 7 && !strncmp(s, "<nosel>", 7))
    {
        assert(state.NoSelectDepth < std::numeric_limits<decltype(state.NoSelectDepth)>::max());
        ++state.NoSelectDepth;
        s += 7;
        return true;
    }
    if (avail >= 8 && !strncmp(s, "</nosel>", 8))
    {
        assert(state.IsInNoSelect());
        --state.NoSelectDepth;
        s += 8;
        return true;
    }
    if (avail >= 4 && s[1] == '/' && s[2] == 'c' && s[3] == '>')
    {
        state.ColorChangeType = ColorChangeType::Pop;
        s += 4;
        return true;
    }
    if (avail >= 4 && s[1] == 'c' && s[2] == '=' && (s[3] == '#' || s[3] == '@' || s[3] == '"' && (avail >= 5 && s[4] == '#' || s[4] == '@')))
    {
        int numColorChars = -1;
        auto colorStart = s + 4;
        if (s[3] == '"')
            ++colorStart;
        for (auto p = colorStart; p < s + avail && numColorChars == -1; ++p)
            if (*p == '"' || *p == '>')
                numColorChars = p - colorStart;
        if (numColorChars == -1)
            return false;
        state.ColorChangeType = ColorChangeType::Push;
        switch (colorStart[-1])
        {
            case '@':
                if (auto const itr = gw2colors.find({ colorStart, colorStart + numColorChars }); itr != gw2colors.end())
                    state.Color = itr->second;
                else
                    return false;
                break;
            case '"':
                state.ColorChangeType = ColorChangeType::PushAlpha;
                state.Color = 0xFF;
                break;
            case '#':
                switch (numColorChars)
                {
                    case 1:
                    {
                        byte a;
                        std::from_chars(colorStart, colorStart + 1, a, 16);
                        state.ColorChangeType = ColorChangeType::PushAlpha;
                        a *= 0x11;
                        state.Color = a;
                        break;
                    }
                    case 2:
                    {
                        byte r, a;
                        std::from_chars(colorStart, colorStart + 1, r, 16);
                        std::from_chars(colorStart + 1, colorStart + 2, a, 16);
                        r *= 0x11;
                        a *= 0x11;
                        state.Color = r | r << 8 | r << 16 | a << 24;
                        break;
                    }
                    case 3:
                    {
                        byte r, g, b;
                        std::from_chars(colorStart, colorStart + 1, r, 16);
                        std::from_chars(colorStart + 1, colorStart + 2, g, 16);
                        std::from_chars(colorStart + 2, colorStart + 3, b, 16);
                        state.ColorChangeType = ColorChangeType::PushColor;
                        r *= 0x11;
                        g *= 0x11;
                        b *= 0x11;
                        state.Color = r | g << 8 | b << 16 | 0xFF << 24;
                        break;
                    }
                    case 4:
                    {
                        byte r, g, b, a;
                        std::from_chars(colorStart, colorStart + 1, r, 16);
                        std::from_chars(colorStart + 1, colorStart + 2, g, 16);
                        std::from_chars(colorStart + 2, colorStart + 3, b, 16);
                        std::from_chars(colorStart + 3, colorStart + 4, a, 16);
                        r *= 0x11;
                        g *= 0x11;
                        b *= 0x11;
                        a *= 0x11;
                        state.Color = r | g << 8 | b << 16 | a << 24;
                        break;
                    }
                    case 6:
                    {
                        byte r, g, b;
                        std::from_chars(colorStart, colorStart + 2, r, 16);
                        std::from_chars(colorStart + 2, colorStart + 4, g, 16);
                        std::from_chars(colorStart + 4, colorStart + 6, b, 16);
                        // state.ColorChangeType = ColorChangeType::PushColor; // Should we?
                        state.Color = r | g << 8 | b << 16 | 0xFF << 24;
                        break;
                    }
                    case 8:
                    {
                        byte r, g, b, a;
                        std::from_chars(colorStart, colorStart + 2, r, 16);
                        std::from_chars(colorStart + 2, colorStart + 4, g, 16);
                        std::from_chars(colorStart + 4, colorStart + 6, b, 16);
                        std::from_chars(colorStart + 6, colorStart + 8, a, 16);
                        state.Color = r | g << 8 | b << 16 | a << 24;
                        break;
                    }
                    default:
                        return false;
                }
                break;
        }
        s = colorStart + numColorChars + 1;
        if (s[-1] == '"' && s[0] == '>')
            ++s;
        return true;
    }
    return false;
}

float BuildLoadGlyphGetAdvanceOrFallback(ImFontBaked* baked, unsigned int codepoint);
// Wrapping skips upcoming blanks
static inline const char* CalcWordWrapNextLineStartA(const char* text, const char* text_end)
{
    while (text < text_end && ImCharIsBlankA(*text))
        text++;
    if (*text == '\n')
        text++;
    return text;
}

MarkupState stateCalcWordWrapPositionA;
const char* ImFont::CalcWordWrapPosition(float size, const char* text, const char* text_end, float wrap_width)
{
    // For references, possible wrap point marked with ^
    //  "aaa bbb, ccc,ddd. eee   fff. ggg!"
    //      ^    ^    ^   ^   ^__    ^    ^

    // List of hardcoded separators: .,;!?'"

    // Skip extra blanks after a line returns (that includes not counting them in width computation)
    // e.g. "Hello    world" --> "Hello" "World"

    // Cut words that cannot possibly fit within one line.
    // e.g.: "The tropical fish" with ~5 characters worth of width --> "The tr" "opical" "fish"

    ImFontBaked* baked = GetFontBaked(size);
    const float scale = size / baked->Size;

    float line_width = 0.0f;
    float word_width = 0.0f;
    float blank_width = 0.0f;
    wrap_width /= scale; // We work with unscaled widths to avoid scaling every characters

    const char* word_end = text;
    const char* prev_word_end = NULL;
    bool inside_word = true;

    const char* s = text;
    IM_ASSERT(text_end != NULL);
    while (s < text_end)
    {
        if (ParseMarkup(s, text_end - s, stateCalcWordWrapPositionA))
            continue;

        unsigned int c = (unsigned int)*s;
        const char* next_s;
        if (c < 0x80)
            next_s = s + 1;
        else
            next_s = s + ImTextCharFromUtf8(&c, s, text_end);

        if (c < 32)
        {
            if (c == '\n')
            {
                line_width = word_width = blank_width = 0.0f;
                inside_word = true;
                s = next_s;
                continue;
            }
            if (c == '\r')
            {
                s = next_s;
                continue;
            }
        }

        // Optimized inline version of 'float char_width = GetCharAdvance((ImWchar)c);'
        float char_width = (c < (unsigned int)baked->IndexAdvanceX.Size) ? baked->IndexAdvanceX.Data[c] : -1.0f;
        if (char_width < 0.0f)
            char_width = BuildLoadGlyphGetAdvanceOrFallback(baked, c);

        if (ImCharIsBlankW(c))
        {
            if (inside_word)
            {
                line_width += blank_width;
                blank_width = 0.0f;
                word_end = s;
            }
            blank_width += char_width;
            inside_word = false;
        }
        else
        {
            word_width += char_width;
            if (inside_word)
            {
                word_end = next_s;
            }
            else
            {
                prev_word_end = word_end;
                line_width += word_width + blank_width;
                word_width = blank_width = 0.0f;
            }

            // Allow wrapping after punctuation.
            inside_word = (c != '.' && c != ',' && c != ';' && c != '!' && c != '?' && c != '\"' && c != 0x3001 && c != 0x3002);
        }

        // We ignore blank width at the end of the line (they can be skipped)
        if (line_width + word_width > wrap_width)
        {
            // Words that cannot possibly fit within an entire line will be cut anywhere.
            if (word_width < wrap_width)
                s = prev_word_end ? prev_word_end : word_end;
            break;
        }

        s = next_s;
    }

    // Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
    // +1 may not be a character start point in UTF-8 but it's ok because caller loops use (text >= word_wrap_eol).
    if (s == text && text < text_end)
        return s + ImTextCountUtf8BytesFromChar(s, text_end);
    return s;
}

ImVec2 ImFont::CalcTextSizeA(float size, float max_width, float wrap_width, const char* text_begin, const char* text_end, const char** remaining)
{
    if (!text_end)
        text_end = text_begin + ImStrlen(text_begin); // FIXME-OPT: Need to avoid this.

    const float line_height = size;
    ImFontBaked* baked = GetFontBaked(size);
    const float scale = size / baked->Size;

    ImVec2 text_size = ImVec2(0, 0);
    float line_width = 0.0f;

    const bool word_wrap_enabled = (wrap_width > 0.0f);
    const char* word_wrap_eol = NULL;

    MarkupState state;
    const char* s = text_begin;
    while (s < text_end)
    {
        if (ParseMarkup(s, text_end - s, state))
            continue;

        if (word_wrap_enabled)
        {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
            if (!word_wrap_eol)
            {
                stateCalcWordWrapPositionA = state;
                word_wrap_eol = CalcWordWrapPosition(size, s, text_end, wrap_width - line_width);
            }

            if (s >= word_wrap_eol)
            {
                if (text_size.x < line_width)
                    text_size.x = line_width;
                text_size.y += line_height;
                line_width = 0.0f;
                word_wrap_eol = NULL;
                s = CalcWordWrapNextLineStartA(s, text_end); // Wrapping skips upcoming blanks
                continue;
            }
        }

        // Decode and advance source
        const char* prev_s = s;
        unsigned int c = (unsigned int)*s;
        if (c < 0x80)
            s += 1;
        else
            s += ImTextCharFromUtf8(&c, s, text_end);

        if (c < 32)
        {
            if (c == '\n')
            {
                text_size.x = ImMax(text_size.x, line_width);
                text_size.y += line_height;
                line_width = 0.0f;
                continue;
            }
            if (c == '\r')
                continue;
        }

        // Optimized inline version of 'float char_width = GetCharAdvance((ImWchar)c);'
        float char_width = (c < (unsigned int)baked->IndexAdvanceX.Size) ? baked->IndexAdvanceX.Data[c] : -1.0f;
        if (char_width < 0.0f)
            char_width = BuildLoadGlyphGetAdvanceOrFallback(baked, c);
        char_width *= scale;

        if (line_width + char_width >= max_width)
        {
            s = prev_s;
            break;
        }

        line_width += char_width;
    }

    if (text_size.x < line_width)
        text_size.x = line_width;

    if (line_width > 0 || text_size.y == 0.0f)
        text_size.y += line_height;

    if (remaining)
        *remaining = s;

    return text_size;
}

// Note: as with every ImDrawList drawing function, this expects that the font atlas texture is bound.
void ImFont::RenderText(ImDrawList* draw_list, float size, const ImVec2& pos, ImU32 col, const ImVec4& clip_rect, const char* text_begin, const char* text_end, float wrap_width, bool cpu_fine_clip)
{
    // Align to be pixel perfect
begin:
    float x = IM_TRUNC(pos.x);
    float y = IM_TRUNC(pos.y);
    if (y > clip_rect.w)
        return;

    if (!text_end)
        text_end = text_begin + ImStrlen(text_begin); // ImGui:: functions generally already provides a valid text_end, so this is merely to handle direct calls.

    const float line_height = size;
    ImFontBaked* baked = GetFontBaked(size);

    const float scale = size / baked->Size;
    const float origin_x = x;
    const bool word_wrap_enabled = (wrap_width > 0.0f);

    // Fast-forward to first visible line
    const char* s = text_begin;
    /*
    stateCalcWordWrapPositionA = { };
    if (y + line_height < clip_rect.y)
        while (y + line_height < clip_rect.y && s < text_end)
        {
            const char* line_end = (const char*)ImMemchr(s, '\n', text_end - s);
            if (word_wrap_enabled)
            {
                // FIXME-OPT: This is not optimal as do first do a search for \n before calling CalcWordWrapPosition().
                // If the specs for CalcWordWrapPosition() were reworked to optionally return on \n we could combine both.
                // However it is still better than nothing performing the fast-forward!
                s = CalcWordWrapPosition(size, s, line_end ? line_end : text_end, wrap_width);
                s = CalcWordWrapNextLineStartA(s, text_end);
            }
            else
            {
                s = line_end ? line_end + 1 : text_end;
            }
            y += line_height;
        }
    */

    // For large text, scan for the last visible line in order to avoid over-reserving in the call to PrimReserve()
    // Note that very large horizontal line will still be affected by the issue (e.g. a one megabyte string buffer without a newline will likely crash atm)
    if (text_end - s > 10000 && !word_wrap_enabled)
    {
        const char* s_end = s;
        float y_end = y;
        while (y_end < clip_rect.w && s_end < text_end)
        {
            s_end = (const char*)ImMemchr(s_end, '\n', text_end - s_end);
            s_end = s_end ? s_end + 1 : text_end;
            y_end += line_height;
        }
        text_end = s_end;
    }
    if (s == text_end)
        return;

    // Reserve vertices for remaining worse case (over-reserving is useful and easily amortized)
    const int vtx_count_max = (int)(text_end - s) * 4 * 2;
    const int idx_count_max = (int)(text_end - s) * 6 * 2;
    const int idx_expected_size = draw_list->IdxBuffer.Size + idx_count_max;
    draw_list->PrimReserve(idx_count_max, vtx_count_max);
    ImDrawVert*  vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx*   idx_write = draw_list->_IdxWritePtr;
    unsigned int vtx_index = draw_list->_VtxCurrentIdx;
    const int cmd_count = draw_list->CmdBuffer.Size;

    ImU32 col_untinted = col | ~IM_COL32_A_MASK;
    const char* word_wrap_eol = NULL;

    std::stack<ImU32> colorStack;

    MarkupState state;
    while (s < text_end)
    {
        if (ParseMarkup(s, text_end - s, state))
        {
            switch (state.ColorChangeType)
            {
                using enum ColorChangeType;
                case Unchanged:
                    break;
                case Push:
                    colorStack.emplace(col);
                    col = state.Color;
                    col_untinted = col | ~IM_COL32_A_MASK;
                    break;
                case PushAlpha:
                    colorStack.emplace(col);
                    col = col & ~IM_COL32_A_MASK | (((col >> 24) * state.Color / 255) & 0xFF) << 24;
                    col_untinted = col | ~IM_COL32_A_MASK;
                    break;
                case PushColor:
                    colorStack.emplace(col);
                    col = col & IM_COL32_A_MASK | state.Color & 0xFFFFFF;
                    col_untinted = col | ~IM_COL32_A_MASK;
                    break;
                case Pop:
                    if (colorStack.empty())
                        break;
                    col = colorStack.top();
                    col_untinted = col | ~IM_COL32_A_MASK;
                    colorStack.pop();
                    break;
                default:
                    std::terminate();
            }
            continue;
        }

        if (word_wrap_enabled)
        {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
            if (!word_wrap_eol)
            {
                stateCalcWordWrapPositionA = state;
                word_wrap_eol = CalcWordWrapPosition(size, s, text_end, wrap_width - (x - origin_x));
            }

            if (s >= word_wrap_eol)
            {
                x = origin_x;
                y += line_height;
                if (y > clip_rect.w)
                    break; // break out of main loop
                word_wrap_eol = NULL;
                s = CalcWordWrapNextLineStartA(s, text_end); // Wrapping skips upcoming blanks
                continue;
            }
        }

        // Decode and advance source
        unsigned int c = (unsigned int)*s;
        if (c < 0x80)
            s += 1;
        else
            s += ImTextCharFromUtf8(&c, s, text_end);

        if (c < 32)
        {
            if (c == '\n')
            {
                x = origin_x;
                y += line_height;
                if (y > clip_rect.w)
                    break; // break out of main loop
                continue;
            }
            if (c == '\r')
                continue;
        }

        const ImFontGlyph* glyph = baked->FindGlyph((ImWchar)c);
        //if (glyph == NULL)
        //    continue;

        float char_width = glyph->AdvanceX * scale;
        if (glyph->Visible)
        {
            // We don't do a second finer clipping test on the Y axis as we've already skipped anything before clip_rect.y and exit once we pass clip_rect.w
            float x1 = x + glyph->X0 * scale;
            float x2 = x + glyph->X1 * scale;
            float y1 = y + glyph->Y0 * scale;
            float y2 = y + glyph->Y1 * scale;
            if (x1 <= clip_rect.z && x2 >= clip_rect.x)
            {
                // Render a character
                float u1 = glyph->U0;
                float v1 = glyph->V0;
                float u2 = glyph->U1;
                float v2 = glyph->V1;

                // CPU side clipping used to fit text in their frame when the frame is too small. Only does clipping for axis aligned quads.
                if (cpu_fine_clip)
                {
                    if (x1 < clip_rect.x)
                    {
                        u1 = u1 + (1.0f - (x2 - clip_rect.x) / (x2 - x1)) * (u2 - u1);
                        x1 = clip_rect.x;
                    }
                    if (y1 < clip_rect.y)
                    {
                        v1 = v1 + (1.0f - (y2 - clip_rect.y) / (y2 - y1)) * (v2 - v1);
                        y1 = clip_rect.y;
                    }
                    if (x2 > clip_rect.z)
                    {
                        u2 = u1 + ((clip_rect.z - x1) / (x2 - x1)) * (u2 - u1);
                        x2 = clip_rect.z;
                    }
                    if (y2 > clip_rect.w)
                    {
                        v2 = v1 + ((clip_rect.w - y1) / (y2 - y1)) * (v2 - v1);
                        y2 = clip_rect.w;
                    }
                    if (y1 >= y2)
                    {
                        x += char_width;
                        continue;
                    }
                }

                // Support for untinted glyphs
                ImU32 glyph_col = glyph->Colored ? col_untinted : col;

                // We are NOT calling PrimRectUV() here because non-inlined causes too much overhead in a debug builds. Inlined here:
                {
                    vtx_write[0].pos.x = x1; vtx_write[0].pos.y = y1; vtx_write[0].col = glyph_col; vtx_write[0].uv.x = u1; vtx_write[0].uv.y = v1;
                    vtx_write[1].pos.x = x2; vtx_write[1].pos.y = y1; vtx_write[1].col = glyph_col; vtx_write[1].uv.x = u2; vtx_write[1].uv.y = v1;
                    vtx_write[2].pos.x = x2; vtx_write[2].pos.y = y2; vtx_write[2].col = glyph_col; vtx_write[2].uv.x = u2; vtx_write[2].uv.y = v2;
                    vtx_write[3].pos.x = x1; vtx_write[3].pos.y = y2; vtx_write[3].col = glyph_col; vtx_write[3].uv.x = u1; vtx_write[3].uv.y = v2;
                    idx_write[0] = (ImDrawIdx)(vtx_index); idx_write[1] = (ImDrawIdx)(vtx_index + 1); idx_write[2] = (ImDrawIdx)(vtx_index + 2);
                    idx_write[3] = (ImDrawIdx)(vtx_index); idx_write[4] = (ImDrawIdx)(vtx_index + 2); idx_write[5] = (ImDrawIdx)(vtx_index + 3);
                    vtx_write += 4;
                    vtx_index += 4;
                    idx_write += 6;
                }

                if (state.BoldDepth)
                {
                    x1 += 0.25f;
                    x2 += 0.25f;
                    vtx_write[0].pos.x = x1; vtx_write[0].pos.y = y1; vtx_write[0].col = glyph_col; vtx_write[0].uv.x = u1; vtx_write[0].uv.y = v1;
                    vtx_write[1].pos.x = x2; vtx_write[1].pos.y = y1; vtx_write[1].col = glyph_col; vtx_write[1].uv.x = u2; vtx_write[1].uv.y = v1;
                    vtx_write[2].pos.x = x2; vtx_write[2].pos.y = y2; vtx_write[2].col = glyph_col; vtx_write[2].uv.x = u2; vtx_write[2].uv.y = v2;
                    vtx_write[3].pos.x = x1; vtx_write[3].pos.y = y2; vtx_write[3].col = glyph_col; vtx_write[3].uv.x = u1; vtx_write[3].uv.y = v2;
                    idx_write[0] = (ImDrawIdx)(vtx_index); idx_write[1] = (ImDrawIdx)(vtx_index + 1); idx_write[2] = (ImDrawIdx)(vtx_index + 2);
                    idx_write[3] = (ImDrawIdx)(vtx_index); idx_write[4] = (ImDrawIdx)(vtx_index + 2); idx_write[5] = (ImDrawIdx)(vtx_index + 3);
                    vtx_write += 4;
                    vtx_index += 4;
                    idx_write += 6;
                }
            }
        }
        x += char_width;
    }

    // Edge case: calling RenderText() with unloaded glyphs triggering texture change. It doesn't happen via ImGui:: calls because CalcTextSize() is always used.
    if (cmd_count != draw_list->CmdBuffer.Size) //-V547
    {
        IM_ASSERT(draw_list->CmdBuffer[draw_list->CmdBuffer.Size - 1].ElemCount == 0);
        draw_list->CmdBuffer.pop_back();
        draw_list->PrimUnreserve(idx_count_max, vtx_count_max);
        draw_list->AddDrawCmd();
        //IMGUI_DEBUG_LOG("RenderText: cancel and retry to missing glyphs.\n"); // [DEBUG]
        //draw_list->AddRectFilled(pos, pos + ImVec2(10, 10), IM_COL32(255, 0, 0, 255)); // [DEBUG]
        goto begin;
        //RenderText(draw_list, size, pos, col, clip_rect, text_begin, text_end, wrap_width, cpu_fine_clip); // FIXME-OPT: Would a 'goto begin' be better for code-gen?
        //return;
    }

    // Give back unused vertices (clipped ones, blanks) ~ this is essentially a PrimUnreserve() action.
    draw_list->VtxBuffer.Size = (int)(vtx_write - draw_list->VtxBuffer.Data); // Same as calling shrink()
    draw_list->IdxBuffer.Size = (int)(idx_write - draw_list->IdxBuffer.Data);
    draw_list->CmdBuffer[draw_list->CmdBuffer.Size - 1].ElemCount -= (idx_expected_size - draw_list->IdxBuffer.Size);
    draw_list->_VtxWritePtr = vtx_write;
    draw_list->_IdxWritePtr = idx_write;
    draw_list->_VtxCurrentIdx = vtx_index;
}

std::string ImGui::StripMarkup(std::string const& str)
{
    std::string stripped(str.length(), 0);
    char* w = stripped.data();
    auto const text_begin = str.data();
    auto const text_end = text_begin + str.length();

    MarkupState state;
    const char* s = text_begin;
    while (s < text_end)
    {
        if (ParseMarkup(s, text_end - s, state))
            continue;

        if (char const c = *s++; !state.IsInNoSelect())
            *w++ = c;
        /*
        if (auto c = (unsigned int)*s; c < 0x80)
            *w++ = *s++;
        else
        {
            auto const len = ImTextCharFromUtf8(&c, s, text_end);
            s += len;

            if (c >= ICON_MIN_FA && c <= ICON_MAX_FA)
                continue;

            ImTextCharToUtf8(w, c);
            w += len;
        }
        */
    }
    stripped.resize(w - stripped.data());
    return stripped;
}

void ImGui::OpenURL(wchar_t const* url)
{
    ShellExecute(nullptr, L"open", url, nullptr, nullptr, SW_SHOW);
}
