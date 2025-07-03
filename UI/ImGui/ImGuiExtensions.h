#pragma once
#include "UI/ImGui/ImGui.h"

import GW2Viewer.Common;
import GW2Viewer.Utils.Encoding;
import std;

namespace I = ImGui;
#define scoped auto _ = dear

#define BITWISE_AND(    ENUM) inline ENUM  operator& (ENUM  a, ENUM b) { return static_cast<ENUM>(static_cast<std::underlying_type_t<ENUM>>(a) & static_cast<std::underlying_type_t<ENUM>>(b)); }
#define BITWISE_OR(     ENUM) inline ENUM  operator| (ENUM  a, ENUM b) { return static_cast<ENUM>(static_cast<std::underlying_type_t<ENUM>>(a) | static_cast<std::underlying_type_t<ENUM>>(b)); }
#define BITWISE_XOR(    ENUM) inline ENUM  operator^ (ENUM  a, ENUM b) { return static_cast<ENUM>(static_cast<std::underlying_type_t<ENUM>>(a) ^ static_cast<std::underlying_type_t<ENUM>>(b)); }
#define BITWISE_AND_SET(ENUM) inline ENUM& operator&=(ENUM& a, ENUM b) { return (a = static_cast<ENUM>(static_cast<std::underlying_type_t<ENUM>>(a) & static_cast<std::underlying_type_t<ENUM>>(b))); }
#define BITWISE_OR_SET( ENUM) inline ENUM& operator|=(ENUM& a, ENUM b) { return (a = static_cast<ENUM>(static_cast<std::underlying_type_t<ENUM>>(a) | static_cast<std::underlying_type_t<ENUM>>(b))); }
#define BITWISE_XOR_SET(ENUM) inline ENUM& operator^=(ENUM& a, ENUM b) { return (a = static_cast<ENUM>(static_cast<std::underlying_type_t<ENUM>>(a) ^ static_cast<std::underlying_type_t<ENUM>>(b))); }
#define BITWISE_ALL(ENUM) BITWISE_AND(ENUM) BITWISE_OR(ENUM) BITWISE_XOR(ENUM) BITWISE_AND_SET(ENUM) BITWISE_OR_SET(ENUM) BITWISE_XOR_SET(ENUM)
BITWISE_ALL(ImGuiButtonFlags_)
#undef BITWISE_AND
#undef BITWISE_OR
#undef BITWISE_XOR
#undef BITWISE_ALL

inline ImVec2 ImRound(ImVec2 const& v) { return { IM_ROUND(v.x), IM_ROUND(v.y) }; }
inline ImVec2 ImRoundIf(ImVec2 const& v, bool condition) { return condition ? ImRound(v) : v; }

namespace ImGui
{
inline ImU32 ColorLerp(ImU32 a, ImU32 b, float t)
{
    auto const av = ColorConvertU32ToFloat4(a);
    auto const bv = ColorConvertU32ToFloat4(b);
    return ColorConvertFloat4ToU32({
        std::lerp(av.x, bv.x, t),
        std::lerp(av.y, bv.y, t),
        std::lerp(av.z, bv.z, t),
        std::lerp(av.w, bv.w, t)
    });
}

static inline constexpr ImGuiID SHARED = 1;
inline ImGuiID GetSharedScopeID(std::string_view str) { return GetIDWithSeed(str.data(), str.data() + str.length(), SHARED); }

inline ImVec2 GetFrameSquare() { return { GetFrameHeight(), GetFrameHeight() }; }

inline bool IsDisabled() { return GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled; }
inline bool IsEnabled() { return !IsDisabled(); }

inline bool CheckboxButton(char const* text, bool& checked, char const* tooltip, ImVec2 const& size = { })
{
    bool const changed = Button(std::format("<c=#{1}>{0}</c>###{0}", text, checked ? "F" : "4").c_str(), size);
    if (tooltip)
        if (scoped::ItemTooltip())
            TextUnformatted(tooltip);
    if (changed)
        checked ^= true;
    return changed;
}
inline bool CheckboxButton(char const* text, bool& checked, char const* tooltip, float const& size = { }) { return CheckboxButton(text, checked, tooltip, { size, size }); }

inline void IndeterminateProgressBar(ImVec2 const& size_arg = ImVec2(-FLT_MIN, 0))
{
    using namespace ImGui;

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiStyle& style = g.Style;
    ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), g.FontSize + style.FramePadding.y * 2.0f);
    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos.x, pos.y, pos.x + size.x, pos.y + size.y);
    ItemSize(size);
    if (!ItemAdd(bb, 0))
        return;

    const float speed = g.FontSize * 0.05f;
    const float phase = ImFmod((float)g.Time * speed, 1.0f);
    const float width_normalized = 0.2f;
    float t0 = phase * (1.0f + width_normalized) - width_normalized;
    float t1 = t0 + width_normalized;

    RenderFrame(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
    bb.Expand(ImVec2(-style.FrameBorderSize, -style.FrameBorderSize));
    RenderRectFilledRangeH(window->DrawList, bb, GetColorU32(ImGuiCol_PlotHistogram), t0, t1, style.FrameRounding);
}

inline bool ComboItem(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0))
{
    bool const result = Selectable(label, selected, flags, size);
    if (selected)
        SetItemDefaultFocus();
    return result;
}

template<typename T, typename... Args>
char const* MakeID(T const& unique, std::format_string<Args...> const format, Args&&... args)
{
    static std::string buffer;
    buffer = std::format(format, std::forward<Args>(args)...);
    buffer = std::format("{}{}---{}", buffer, buffer.contains("##") ? "" : "##", (uintptr_t)unique);
    return buffer.c_str();
}

auto InputTextUTF8(char const* label, auto& container, auto const& key, std::wstring_view placeholder)
{
    static std::string utf8;
    auto itr = container.find(key);
    if (itr != container.end())
        utf8 = GW2Viewer::Utils::Encoding::ToUTF8(itr->second);
    else
        utf8.clear();
    if (InputTextWithHint(label, GW2Viewer::Utils::Encoding::ToUTF8(placeholder).c_str(), &utf8))
    {
        if (itr != container.end())
            itr->second = GW2Viewer::Utils::Encoding::FromUTF8(utf8);
        else
            container.emplace(key, GW2Viewer::Utils::Encoding::FromUTF8(utf8)).first;
        return true;
    }
    return false;
};

std::string StripMarkup(std::string const& str);
inline std::wstring StripMarkup(std::wstring const& str)
{
    return GW2Viewer::Utils::Encoding::FromUTF8(StripMarkup(GW2Viewer::Utils::Encoding::ToUTF8(str)));
}

inline bool InputTextReadOnly(const char* label, std::string const& str, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr)
{
    auto size = CalcTextSize(str.c_str(), str.c_str() + str.size()) + GetStyle().FramePadding * 2;
    if (GImGui->NextItemData.Flags & ImGuiNextItemDataFlags_HasWidth)
        size.x = CalcItemWidth();
    auto const avail = GetContentRegionAvail();
    if (size.x > avail.x)
        size.x = avail.x;
    if (size.x < 10)
        size.x = 10;
    if (GImGui->ActiveId == GetID(label))
    {
        auto const stripped = StripMarkup(str);
        return InputTextEx(label, nullptr, (char*)stripped.c_str(), stripped.capacity() + 1, size, flags | ImGuiInputTextFlags_ReadOnly, callback, user_data);
    }
    return InputTextEx(label, nullptr, (char*)str.c_str(), str.capacity() + 1, size, flags | ImGuiInputTextFlags_ReadOnly, callback, user_data);
}

[[nodiscard]] inline ImGuiButtonFlags_ IsItemMouseClickedWith(ImGuiButtonFlags_ buttons)
{
    auto const& item = GetCurrentContext()->LastItemData;
    auto clicked = ImGuiButtonFlags_None;
    if (GetCurrentContext()->NavActivateId == item.ID)
    {
        if (GetIO().KeyCtrl || IsKeyDown(ImGuiKey_LeftCtrl) || IsKeyDown(ImGuiKey_RightCtrl) || IsKeyDown(ImGuiKey_ModCtrl))
            clicked |= ImGuiButtonFlags_MouseButtonMiddle;
        else if (GetIO().KeyAlt || IsKeyDown(ImGuiKey_LeftAlt) || IsKeyDown(ImGuiKey_RightAlt) || IsKeyDown(ImGuiKey_ModAlt))
            clicked |= ImGuiButtonFlags_MouseButtonRight;
        else
            clicked |= ImGuiButtonFlags_MouseButtonLeft;
        return clicked;
    }

    if (!(item.StatusFlags & ImGuiItemStatusFlags_HoveredRect) || !IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        return clicked;

    if (IsMouseClicked(ImGuiMouseButton_Left))
        clicked |= ImGuiButtonFlags_MouseButtonLeft;
    if (IsMouseClicked(ImGuiMouseButton_Right))
        clicked |= ImGuiButtonFlags_MouseButtonRight;
    if (IsMouseClicked(ImGuiMouseButton_Middle))
        clicked |= ImGuiButtonFlags_MouseButtonMiddle;

    return buttons & clicked;
}

void OpenURL(wchar_t const* url);


    static float GetMinimumStepAtDecimalPrecision(int decimal_precision)
    {
        static const float min_steps[10] = { 1.0f, 0.1f, 0.01f, 0.001f, 0.0001f, 0.00001f, 0.000001f, 0.0000001f, 0.00000001f, 0.000000001f };
        if (decimal_precision < 0)
            return FLT_MIN;
        return (decimal_precision < IM_ARRAYSIZE(min_steps)) ? min_steps[decimal_precision] : ImPow(10.0f, (float)-decimal_precision);
    }

// This is called by DragBehavior() when the widget is active (held by mouse or being manipulated with Nav controls)
template<typename TYPE, typename SIGNEDTYPE, typename FLOATTYPE, typename COERCE>
bool DragCoerceBehaviorT(ImGuiDataType data_type, TYPE* v, float v_speed, const TYPE v_min, const TYPE v_max, const char* format, ImGuiSliderFlags flags, COERCE&& coerce)
{
    ImGuiContext& g = *GImGui;
    const ImGuiAxis axis = (flags & ImGuiSliderFlags_Vertical) ? ImGuiAxis_Y : ImGuiAxis_X;
    const bool is_clamped = (v_min < v_max);
    const bool is_logarithmic = (flags & ImGuiSliderFlags_Logarithmic) != 0;
    const bool is_floating_point = (data_type == ImGuiDataType_Float) || (data_type == ImGuiDataType_Double);

    // Default tweak speed
    if (v_speed == 0.0f && is_clamped && (v_max - v_min < FLT_MAX))
        v_speed = (float)((v_max - v_min) * g.DragSpeedDefaultRatio);

    // Inputs accumulates into g.DragCurrentAccum, which is flushed into the current value as soon as it makes a difference with our precision settings
    float adjust_delta = 0.0f;
    if (g.ActiveIdSource == ImGuiInputSource_Mouse && IsMousePosValid() && IsMouseDragPastThreshold(0, g.IO.MouseDragThreshold * 0.50f))
    {
        adjust_delta = g.IO.MouseDelta[axis];
        if (g.IO.KeyAlt)
            adjust_delta *= 1.0f / 100.0f;
        if (g.IO.KeyShift)
            adjust_delta *= 10.0f;
    }
    else if (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad)
    {
        const int decimal_precision = is_floating_point ? ImParseFormatPrecision(format, 3) : 0;
        const bool tweak_slow = IsKeyDown((g.NavInputSource == ImGuiInputSource_Gamepad) ? ImGuiKey_NavGamepadTweakSlow : ImGuiKey_NavKeyboardTweakSlow);
        const bool tweak_fast = IsKeyDown((g.NavInputSource == ImGuiInputSource_Gamepad) ? ImGuiKey_NavGamepadTweakFast : ImGuiKey_NavKeyboardTweakFast);
        const float tweak_factor = tweak_slow ? 1.0f / 1.0f : tweak_fast ? 10.0f : 1.0f;
        adjust_delta = GetNavTweakPressedAmount(axis) * tweak_factor;
        v_speed = ImMax(v_speed, GetMinimumStepAtDecimalPrecision(decimal_precision));
    }
    adjust_delta *= v_speed;

    // For vertical drag we currently assume that Up=higher value (like we do with vertical sliders). This may become a parameter.
    if (axis == ImGuiAxis_Y)
        adjust_delta = -adjust_delta;

    // For logarithmic use our range is effectively 0..1 so scale the delta into that range
    if (is_logarithmic && (v_max - v_min < FLT_MAX) && ((v_max - v_min) > 0.000001f)) // Epsilon to avoid /0
        adjust_delta /= (float)(v_max - v_min);

    // Clear current value on activation
    // Avoid altering values and clamping when we are _already_ past the limits and heading in the same direction, so e.g. if range is 0..255, current value is 300 and we are pushing to the right side, keep the 300.
    bool is_just_activated = g.ActiveIdIsJustActivated;
    bool is_already_past_limits_and_pushing_outward = is_clamped && ((*v >= v_max && adjust_delta > 0.0f) || (*v <= v_min && adjust_delta < 0.0f));
    if (is_just_activated || is_already_past_limits_and_pushing_outward)
    {
        g.DragCurrentAccum = 0.0f;
        g.DragCurrentAccumDirty = false;
    }
    else if (adjust_delta != 0.0f)
    {
        g.DragCurrentAccum += adjust_delta;
        g.DragCurrentAccumDirty = true;
    }

    if (!g.DragCurrentAccumDirty)
        return false;

    TYPE v_cur = *v;
    FLOATTYPE v_old_ref_for_accum_remainder = (FLOATTYPE)0.0f;

    float logarithmic_zero_epsilon = 0.0f; // Only valid when is_logarithmic is true
    const float zero_deadzone_halfsize = 0.0f; // Drag widgets have no deadzone (as it doesn't make sense)
    if (is_logarithmic)
    {
        // When using logarithmic sliders, we need to clamp to avoid hitting zero, but our choice of clamp value greatly affects slider precision. We attempt to use the specified precision to estimate a good lower bound.
        const int decimal_precision = is_floating_point ? ImParseFormatPrecision(format, 3) : 1;
        logarithmic_zero_epsilon = ImPow(0.1f, (float)decimal_precision);

        // Convert to parametric space, apply delta, convert back
        float v_old_parametric = ScaleRatioFromValueT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, v_cur, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);
        float v_new_parametric = v_old_parametric + g.DragCurrentAccum;
        v_cur = ScaleValueFromRatioT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, v_new_parametric, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);
        v_old_ref_for_accum_remainder = v_old_parametric;
    }
    else
    {
        v_cur += (SIGNEDTYPE)g.DragCurrentAccum;
    }

    v_cur = coerce(v_cur);

    // Round to user desired precision based on format string
    if (is_floating_point && !(flags & ImGuiSliderFlags_NoRoundToFormat))
        v_cur = RoundScalarWithFormatT<TYPE>(format, data_type, v_cur);

    // Preserve remainder after rounding has been applied. This also allow slow tweaking of values.
    g.DragCurrentAccumDirty = false;
    if (is_logarithmic)
    {
        // Convert to parametric space, apply delta, convert back
        float v_new_parametric = ScaleRatioFromValueT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, v_cur, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);
        g.DragCurrentAccum -= (float)(v_new_parametric - v_old_ref_for_accum_remainder);
    }
    else
    {
        g.DragCurrentAccum -= (float)((SIGNEDTYPE)v_cur - (SIGNEDTYPE)*v);
    }

    // Lose zero sign for float/double
    if (v_cur == (TYPE)-0)
        v_cur = (TYPE)0;

    // Clamp values (+ handle overflow/wrap-around for integer types)
    if (*v != v_cur && is_clamped)
    {
        if (v_cur < v_min || (v_cur > *v && adjust_delta < 0.0f && !is_floating_point))
            v_cur = v_min;
        if (v_cur > v_max || (v_cur < *v && adjust_delta > 0.0f && !is_floating_point))
            v_cur = v_max;
    }

    // Apply result
    if (*v == v_cur)
        return false;
    *v = v_cur;
    return true;
}

template<ImGuiDataType_ data_type, typename COERCE>
bool DragCoerceBehavior(ImGuiID id, void* p_v, float v_speed, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags, COERCE&& coerce)
{
    // Read imgui.cpp "API BREAKING CHANGES" section for 1.78 if you hit this assert.
    IM_ASSERT((flags == 1 || (flags & ImGuiSliderFlags_InvalidMask_) == 0) && "Invalid ImGuiSliderFlags flags! Has the 'float power' argument been mistakenly cast to flags? Call function with ImGuiSliderFlags_Logarithmic flags instead.");

    ImGuiContext& g = *GImGui;
    if (g.ActiveId == id)
    {
        // Those are the things we can do easily outside the DragBehaviorT<> template, saves code generation.
        if (g.ActiveIdSource == ImGuiInputSource_Mouse && !g.IO.MouseDown[0])
            ClearActiveID();
        else if ((g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad) && g.NavActivatePressedId == id && !g.ActiveIdIsJustActivated)
            ClearActiveID();
    }
    if (g.ActiveId != id)
        return false;
    if ((g.LastItemData.InFlags & ImGuiItemFlags_ReadOnly) || (flags & ImGuiSliderFlags_ReadOnly))
        return false;

    static_assert(data_type != ImGuiDataType_COUNT);
         if constexpr (data_type == ImGuiDataType_S8)     { ImS32 v32 = (ImS32)*(ImS8*)p_v;  bool r = DragCoerceBehaviorT<ImS32, ImS32, float>(ImGuiDataType_S32, &v32, v_speed, p_min ? *(const ImS8*) p_min : -128,  p_max ? *(const ImS8*)p_max  : 127,  format, flags, std::forward<COERCE>(coerce)); if (r) *(ImS8*)p_v = (ImS8)v32; return r; }
    else if constexpr (data_type == ImGuiDataType_U8)     { ImU32 v32 = (ImU32)*(ImU8*)p_v;  bool r = DragCoerceBehaviorT<ImU32, ImS32, float>(ImGuiDataType_U32, &v32, v_speed, p_min ? *(const ImU8*) p_min : 0,  p_max ? *(const ImU8*)p_max  : 0xFF,  format, flags, std::forward<COERCE>(coerce)); if (r) *(ImU8*)p_v = (ImU8)v32; return r; }
    else if constexpr (data_type == ImGuiDataType_S16)    { ImS32 v32 = (ImS32)*(ImS16*)p_v; bool r = DragCoerceBehaviorT<ImS32, ImS32, float>(ImGuiDataType_S32, &v32, v_speed, p_min ? *(const ImS16*)p_min : -32768, p_max ? *(const ImS16*)p_max : 32767, format, flags, std::forward<COERCE>(coerce)); if (r) *(ImS16*)p_v = (ImS16)v32; return r; }
    else if constexpr (data_type == ImGuiDataType_U16)    { ImU32 v32 = (ImU32)*(ImU16*)p_v; bool r = DragCoerceBehaviorT<ImU32, ImS32, float>(ImGuiDataType_U32, &v32, v_speed, p_min ? *(const ImU16*)p_min : 0, p_max ? *(const ImU16*)p_max : 0xFFFF, format, flags, std::forward<COERCE>(coerce)); if (r) *(ImU16*)p_v = (ImU16)v32; return r; }
    else if constexpr (data_type == ImGuiDataType_S32)    return DragCoerceBehaviorT<ImS32, ImS32, float >(data_type, (ImS32*)p_v,  v_speed, p_min ? *(const ImS32* )p_min : INT_MIN, p_max ? *(const ImS32* )p_max : INT_MAX, format, flags, std::forward<COERCE>(coerce));
    else if constexpr (data_type == ImGuiDataType_U32)    return DragCoerceBehaviorT<ImU32, ImS32, float >(data_type, (ImU32*)p_v,  v_speed, p_min ? *(const ImU32* )p_min : 0, p_max ? *(const ImU32* )p_max : UINT_MAX, format, flags, std::forward<COERCE>(coerce));
    else if constexpr (data_type == ImGuiDataType_S64)    return DragCoerceBehaviorT<ImS64, ImS64, double>(data_type, (ImS64*)p_v,  v_speed, p_min ? *(const ImS64* )p_min : LLONG_MIN, p_max ? *(const ImS64* )p_max : LLONG_MAX, format, flags, std::forward<COERCE>(coerce));
    else if constexpr (data_type == ImGuiDataType_U64)    return DragCoerceBehaviorT<ImU64, ImS64, double>(data_type, (ImU64*)p_v,  v_speed, p_min ? *(const ImU64* )p_min : 0, p_max ? *(const ImU64* )p_max : ULLONG_MAX, format, flags, std::forward<COERCE>(coerce));
    else if constexpr (data_type == ImGuiDataType_Float)  return DragCoerceBehaviorT<float, float, float >(data_type, (float*)p_v,  v_speed, p_min ? *(const float* )p_min : -FLT_MAX,   p_max ? *(const float* )p_max : FLT_MAX,    format, flags, std::forward<COERCE>(coerce));
    else if constexpr (data_type == ImGuiDataType_Double) return DragCoerceBehaviorT<double,double,double>(data_type, (double*)p_v, v_speed, p_min ? *(const double*)p_min : -DBL_MAX,   p_max ? *(const double*)p_max : DBL_MAX,    format, flags, std::forward<COERCE>(coerce));
    IM_ASSERT(0);
    return false;
}

// Note: p_data, p_min and p_max are _pointers_ to a memory address holding the data. For a Drag widget, p_min and p_max are optional.
// Read code of e.g. DragFloat(), DragInt() etc. or examples in 'Demo->Widgets->Data Types' to understand how to use this function directly.
template<ImGuiDataType_ data_type, typename COERCE>
bool DragCoerceScalar(const char* label, void* p_data, float v_speed = 1.0f, const void* p_min = NULL, const void* p_max = NULL, const char* format = NULL, ImGuiSliderFlags flags = 0, COERCE&& coerce = [](auto v) { return v; })
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const float w = CalcItemWidth();

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
    const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));

    const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    ItemSize(total_bb, style.FramePadding.y);
    if (!ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
        return false;

    // Default format string when passing NULL
    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.InFlags);
    bool temp_input_is_active = temp_input_allowed && TempInputIsActive(id);
    if (!temp_input_is_active)
    {
        // Tabbing or CTRL-clicking on Drag turns it into an InputText
        const bool clicked = hovered && IsMouseClicked(0, id);
        const bool double_clicked = (hovered && g.IO.MouseClickedCount[0] == 2 && TestKeyOwner(ImGuiKey_MouseLeft, id));
        const bool make_active = (clicked || double_clicked || g.NavActivateId == id);
        if (make_active && (clicked || double_clicked))
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        if (make_active && temp_input_allowed)
            if ((clicked && g.IO.KeyCtrl) || double_clicked || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input_is_active = true;

        // (Optional) simple click (without moving) turns Drag into an InputText
        if (g.IO.ConfigDragClickToInputText && temp_input_allowed && !temp_input_is_active)
            if (g.ActiveId == id && hovered && g.IO.MouseReleased[0] && !IsMouseDragPastThreshold(0, g.IO.MouseDragThreshold * 0.50f))
            {
                g.NavActivateId = id;
                g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
                temp_input_is_active = true;
            }

        if (make_active && !temp_input_is_active)
        {
            SetActiveID(id, window);
            SetFocusID(id, window);
            FocusWindow(window);
            g.ActiveIdUsingNavDirMask = (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }
    }

    if (temp_input_is_active)
    {
        // Only clamp CTRL+Click input when ImGuiSliderFlags_AlwaysClamp is set
        const bool is_clamp_input = (flags & ImGuiSliderFlags_AlwaysClamp) != 0 && (p_min == NULL || p_max == NULL || DataTypeCompare(data_type, p_min, p_max) < 0);
        return TempInputScalar(frame_bb, id, label, data_type, p_data, format, is_clamp_input ? p_min : NULL, is_clamp_input ? p_max : NULL);
    }

    // Draw frame
    const ImU32 frame_col = GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    RenderNavHighlight(frame_bb, id);
    RenderFrame(frame_bb.Min, frame_bb.Max, frame_col, true, style.FrameRounding);

    // Drag behavior
    const bool value_changed = DragCoerceBehavior<data_type>(id, p_data, v_speed, p_min, p_max, format, flags, std::forward<COERCE>(coerce));
    if (value_changed)
        MarkItemEdited(id);

    // Display value using user-provided display format so user can add prefix/suffix/decorations to the value.
    char value_buf[64];
    const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
    if (g.LogEnabled)
        LogSetNextTextDecoration("{", "}");
    RenderTextClipped(frame_bb.Min, frame_bb.Max, value_buf, value_buf_end, NULL, ImVec2(0.5f, 0.5f));

    if (label_size.x > 0.0f)
        RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | (temp_input_allowed ? ImGuiItemStatusFlags_Inputable : 0));
    return value_changed;
}

template<typename COERCE>
bool DragCoerceFloat(const char* label, float* v, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0, COERCE&& coerce = [](auto v) { return v; })
{
    return DragCoerceScalar<ImGuiDataType_Float>(label, v, v_speed, &v_min, &v_max, format, flags, std::forward<COERCE>(coerce));
}

template<typename COERCE>
bool DragCoerceInt(const char* label, int* v, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0, COERCE&& coerce = [](auto v) { return v; })
{
    return DragCoerceScalar<ImGuiDataType_S32>(label, v, v_speed, &v_min, &v_max, format, flags, std::forward<COERCE>(coerce));
}

}

namespace dear
{
    struct WithCursorPos : ScopeWrapper<WithCursorPos>
    {
        WithCursorPos(ImVec2 const& pos) noexcept : ScopeWrapper(true) { ImGui::SetCursorPos(pos); }
        WithCursorPos(float x, float y) noexcept : WithCursorPos(ImVec2(x, y)) { }
        void dtor() noexcept { ImGui::SetCursorPos(restore); ImGui::GetCurrentWindow()->DC.IsSetPos = false; }
        ImVec2 const restore = ImGui::GetCursorPos();
    };
    struct WithCursorScreenPos : ScopeWrapper<WithCursorScreenPos>
    {
        WithCursorScreenPos(ImVec2 const& pos) noexcept : ScopeWrapper(true) { ImGui::SetCursorScreenPos(pos); }
        WithCursorScreenPos(float x, float y) noexcept : WithCursorScreenPos(ImVec2(x, y)) { }
        void dtor() noexcept { ImGui::SetCursorScreenPos(restore); ImGui::GetCurrentWindow()->DC.IsSetPos = false; }
        ImVec2 const restore = ImGui::GetCursorScreenPos();
    };
    struct WithCursorOffset : ScopeWrapper<WithCursorOffset>
    {
        WithCursorOffset(ImVec2 const& offset) noexcept : ScopeWrapper(true) { ImGui::SetCursorScreenPos(restore + offset); }
        WithCursorOffset(float x, float y) noexcept : WithCursorOffset(ImVec2(x, y)) { }
        void dtor() noexcept { ImGui::SetCursorScreenPos(restore); ImGui::GetCurrentWindow()->DC.IsSetPos = false; }
        ImVec2 const restore = ImGui::GetCursorScreenPos();
    };
    struct WithColorVar : ScopeWrapper<WithColorVar>
    {
        WithColorVar(ImGuiCol idx, ImVec4 const& col) noexcept : ScopeWrapper(true) { ImGui::PushStyleColor(idx, col); }
        WithColorVar(ImGuiStyleVar idx, ImU32 col) noexcept : ScopeWrapper(true) { ImGui::PushStyleColor(idx, col); }
        static void dtor() noexcept { ImGui::PopStyleColor(); }
    };
    struct Font : ScopeWrapper<Font>
    {
        Font(ImFont* font) noexcept : ScopeWrapper(true) { ImGui::PushFont(font); }
        static void dtor() noexcept { ImGui::PopFont(); }
    };
    struct PopupContextItem : ScopeWrapper<PopupContextItem>
    {
        PopupContextItem(const char* str_id = NULL, ImGuiPopupFlags popup_flags = 1) noexcept : ScopeWrapper(ImGui::BeginPopupContextItem(str_id, popup_flags)) { }
        static void dtor() noexcept { ImGui::EndPopup(); }
    };
}






#pragma region Combo With Filtering
namespace ImGui
{

    // https://github.com/forrestthewoods/lib_fts

    // Forward declarations for "private" implementation
    namespace fuzzy_internal {
        static bool fuzzy_match_recursive(const char* pattern, const char* str, int& outScore, const char* strBegin,
            uint8_t const* srcMatches, uint8_t* newMatches, int maxMatches, int nextMatch,
            int& recursionCount, int recursionLimit);
    }
    // Private implementation
    static bool fuzzy_internal::fuzzy_match_recursive(const char* pattern, const char* str, int& outScore,
        const char* strBegin, uint8_t const* srcMatches, uint8_t* matches, int maxMatches,
        int nextMatch, int& recursionCount, int recursionLimit)
    {
        // Count recursions
        ++recursionCount;
        if (recursionCount >= recursionLimit)
            return false;

        // Detect end of strings
        if (*pattern == '\0' || *str == '\0')
            return false;

        // Recursion params
        bool recursiveMatch = false;
        uint8_t bestRecursiveMatches[256];
        int bestRecursiveScore = 0;

        // Loop through pattern and str looking for a match
        bool first_match = true;
        while (*pattern != '\0' && *str != '\0') {

            // Found match
            if (tolower(*pattern) == tolower(*str)) {

                // Supplied matches buffer was too short
                if (nextMatch >= maxMatches)
                    return false;

                // "Copy-on-Write" srcMatches into matches
                if (first_match && srcMatches) {
                    memcpy(matches, srcMatches, nextMatch);
                    first_match = false;
                }

                // Recursive call that "skips" this match
                uint8_t recursiveMatches[256];
                int recursiveScore;
                if (fuzzy_match_recursive(pattern, str + 1, recursiveScore, strBegin, matches, recursiveMatches, sizeof(recursiveMatches), nextMatch, recursionCount, recursionLimit)) {

                    // Pick best recursive score
                    if (!recursiveMatch || recursiveScore > bestRecursiveScore) {
                        memcpy(bestRecursiveMatches, recursiveMatches, 256);
                        bestRecursiveScore = recursiveScore;
                    }
                    recursiveMatch = true;
                }

                // Advance
                matches[nextMatch++] = (uint8_t)(str - strBegin);
                ++pattern;
            }
            ++str;
        }

        // Determine if full pattern was matched
        bool matched = *pattern == '\0' ? true : false;

        // Calculate score
        if (matched) {
            const int sequential_bonus = 15;            // bonus for adjacent matches
            const int separator_bonus = 30;             // bonus if match occurs after a separator
            const int camel_bonus = 30;                 // bonus if match is uppercase and prev is lower
            const int first_letter_bonus = 15;          // bonus if the first letter is matched

            const int leading_letter_penalty = -5;      // penalty applied for every letter in str before the first match
            const int max_leading_letter_penalty = -15; // maximum penalty for leading letters
            const int unmatched_letter_penalty = -1;    // penalty for every letter that doesn't matter

            // Iterate str to end
            while (*str != '\0')
                ++str;

            // Initialize score
            outScore = 100;

            // Apply leading letter penalty
            int penalty = leading_letter_penalty * matches[0];
            if (penalty < max_leading_letter_penalty)
                penalty = max_leading_letter_penalty;
            outScore += penalty;

            // Apply unmatched penalty
            int unmatched = (int)(str - strBegin) - nextMatch;
            outScore += unmatched_letter_penalty * unmatched;

            // Apply ordering bonuses
            for (int i = 0; i < nextMatch; ++i) {
                uint8_t currIdx = matches[i];

                if (i > 0) {
                    uint8_t prevIdx = matches[i - 1];

                    // Sequential
                    if (currIdx == (prevIdx + 1))
                        outScore += sequential_bonus;
                }

                // Check for bonuses based on neighbor character value
                if (currIdx > 0) {
                    // Camel case
                    char neighbor = strBegin[currIdx - 1];
                    char curr = strBegin[currIdx];
                    if (::islower(neighbor) && ::isupper(curr))
                        outScore += camel_bonus;

                    // Separator
                    bool neighborSeparator = neighbor == '_' || neighbor == ' ';
                    if (neighborSeparator)
                        outScore += separator_bonus;
                }
                else {
                    // First letter
                    outScore += first_letter_bonus;
                }
            }
        }

        // Return best result
        if (recursiveMatch && (!matched || bestRecursiveScore > outScore)) {
            // Recursive score is better than "this"
            memcpy(matches, bestRecursiveMatches, maxMatches);
            outScore = bestRecursiveScore;
            return true;
        }
        else if (matched) {
            // "this" score is better than recursive
            return true;
        }
        else {
            // no match
            return false;
        }
    }



    static bool fuzzy_match(char const* pattern, char const* str, int& outScore, uint8_t* matches, int maxMatches) {
        int recursionCount = 0;
        int recursionLimit = 10;

        return fuzzy_internal::fuzzy_match_recursive(pattern, str, outScore, str, nullptr, matches, maxMatches, 0, recursionCount, recursionLimit);
    }

    // Public interface
    static bool fuzzy_match_simple(char const* pattern, char const* str) {
        while (*pattern != '\0' && *str != '\0') {
            if (tolower(*pattern) == tolower(*str))
                ++pattern;
            ++str;
        }

        return *pattern == '\0' ? true : false;
    }

    static bool fuzzy_match(char const* pattern, char const* str, int& outScore) {

        uint8_t matches[256];
        return fuzzy_match(pattern, str, outScore, matches, sizeof(matches));
    }

    static bool sortbysec_desc(const std::pair<int, int>& a, const std::pair<int, int>& b)
    {
        return (b.second < a.second);
    }

    static bool ComboWithFilter(const char* label, int* current_item, const std::vector<std::string>& items)
    {
        ImGuiContext& g = *GImGui;

        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const ImGuiStyle& style = g.Style;
        int items_count = items.size();

        // Call the getter to obtain the preview string which is a parameter to BeginCombo()
        const char* preview_value = NULL;
        if (*current_item >= 0 && *current_item < items_count)
            preview_value = items[*current_item].c_str();

        static char pattern_buffer[256] = { 0 };
        bool isNeedFilter = false;

        char comboButtonName[512] = { 0 };
        ImFormatString(comboButtonName, IM_ARRAYSIZE(comboButtonName), "%s##name_ComboWithFilter_button_%s", preview_value ? preview_value : "", label);

        char name_popup[256 + 10];
        ImFormatString(name_popup, IM_ARRAYSIZE(name_popup), "##name_popup_%s", label);

        // Display items
        // FIXME-OPT: Use clipper (but we need to disable it on the appearing frame to make sure our call to SetItemDefaultFocus() is processed)
        bool value_changed = false;

        const float expected_w = CalcItemWidth();
        ImVec2 item_min = GetItemRectMin();
        bool isNewOpen = false;
        float sz = GetFrameHeight();
        ImVec2 size(sz, sz);
        ImVec2 CursorPos = window->DC.CursorPos;
        ImVec2 pos = CursorPos + ImVec2(expected_w - sz, 0);
        const ImRect bb(pos, pos + size);

        float ButtonTextAlignX = g.Style.ButtonTextAlign.x;
        g.Style.ButtonTextAlign.x = 0;
        if (ImGui::Button(comboButtonName, ImVec2(expected_w, 0)))
        {
            ImGui::OpenPopup(name_popup);
            isNewOpen = true;
        }
        g.Style.ButtonTextAlign.x = ButtonTextAlignX;
        bool hovered = IsItemHovered();
        bool active = IsItemActivated();
        bool pressed = IsItemClicked();

        // Render
        //const ImU32 bg_col = GetColorU32((active && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        //RenderFrame(bb.Min, bb.Max, bg_col, true, g.Style.FrameRounding);
        const ImU32 text_col = GetColorU32(ImGuiCol_Text);
        RenderArrow(window->DrawList, bb.Min + ImVec2(ImMax(0.0f, (size.x - g.FontSize) * 0.5f), ImMax(0.0f, (size.y - g.FontSize) * 0.5f)), text_col, ImGuiDir_Down);

        if (isNewOpen)
        {
            memset(pattern_buffer, 0, IM_ARRAYSIZE(pattern_buffer));
        }
        ImVec2 item_max = GetItemRectMax();
        SetNextWindowPos({ CursorPos.x, item_max.y });
        ImGui::SetNextWindowSize({ ImGui::GetItemRectSize().x, 0 });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(g.Style.FramePadding.x, g.Style.FramePadding.x)); // Horizontally align ourselves with the framed text
        if (ImGui::BeginPopup(name_popup))
        {
            ImGui::PushItemWidth(-FLT_MIN);
            // Filter input
            if (isNewOpen)
                ImGui::SetKeyboardFocusHere();
            InputText("##ComboWithFilter_inputText", pattern_buffer, 256);

            // Search Icon, you can use it if you load IconsFontAwesome5 https://github.com/juliettef/IconFontCppHeaders
            //const ImVec2 label_size = CalcTextSize(ICON_FA_SEARCH, NULL, true);
            //const ImVec2 search_icon_pos(ImGui::GetItemRectMax().x - label_size.x - style.ItemInnerSpacing.x * 2, window->DC.CursorPos.y + style.FramePadding.y + g.FontSize * 0.1f);
            //RenderText(search_icon_pos, ICON_FA_SEARCH);

            if (pattern_buffer[0] != '\0')
            {
                isNeedFilter = true;
            }

            std::vector<std::pair<int, int> > itemScoreVector;
            if (isNeedFilter)
            {
                for (int i = 0; i < items_count; i++)
                {
                    int score = 0;
                    bool matched = fuzzy_match(pattern_buffer, items[i].c_str(), score);
                    if (matched)
                        itemScoreVector.push_back(std::make_pair(i, score));
                }
                std::sort(itemScoreVector.begin(), itemScoreVector.end(), sortbysec_desc);
            }

            items_count = isNeedFilter ? itemScoreVector.size() : items_count;
            int height_in_items = -1;
            if (height_in_items < 0)
                height_in_items = ImMin(items_count, 7);
            const ImGuiStyle& style = GetStyle();
            float height_in_items_f = (height_in_items < items_count) ? (height_in_items + 0.25f) : (height_in_items + 0.00f);
            ImVec2 size;
            size.x = 0.0f;
            size.y = ImFloor(GetTextLineHeightWithSpacing() * height_in_items_f + style.FramePadding.y * 2.0f);
            if (ImGui::BeginChild("##ComboWithFilter_itemList", size))
            {
                for (int i = 0; i < items_count; i++)
                {
                    int idx = isNeedFilter ? itemScoreVector[i].first : i;
                    PushID((void*)(intptr_t)idx);
                    const bool item_selected = (idx == *current_item);
                    const char* item_text = items[idx].c_str();
                    if (Selectable(item_text, item_selected))
                    {
                        value_changed = true;
                        *current_item = idx;
                        CloseCurrentPopup();
                    }
                    if (item_selected)
                        SetItemDefaultFocus();
                    PopID();
                }
                ImGui::EndChild();
            }
            ImGui::PopItemWidth();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();

        if (value_changed)
            MarkItemEdited(g.LastItemData.ID);

        return value_changed;
    }
}
#pragma endregion