module;
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

export module GW2Viewer.UI.ImGui:Platform;

export
{

#pragma region imgui_impl_dx11.h
using ::ID3D11Device;
using ::ID3D11DeviceContext;
using ::ID3D11SamplerState;
using ::ID3D11Buffer;

using ::ImGui_ImplDX11_Init;
using ::ImGui_ImplDX11_Shutdown;
using ::ImGui_ImplDX11_NewFrame;
using ::ImGui_ImplDX11_RenderDrawData;

using ::ImGui_ImplDX11_CreateDeviceObjects;
using ::ImGui_ImplDX11_InvalidateDeviceObjects;

using ::ImGui_ImplDX11_UpdateTexture;

using ::ImGui_ImplDX11_RenderState;

using ::ImGui_ImplDX11_CompilePixelShader;
using ::ImGui_ImplDX11_SetPixelShader;
using ::ImGui_ImplDX11_CreateBuffer;
using ::ImGui_ImplDX11_SetPixelShaderConstantBuffer;
using ::ImGui_ImplDX11_SetPixelShaderShaderResource;
#pragma endregion

#pragma region imgui_impl_win32.h
using ::ImGui_ImplWin32_Init;
using ::ImGui_ImplWin32_InitForOpenGL;
using ::ImGui_ImplWin32_Shutdown;
using ::ImGui_ImplWin32_NewFrame;

using ::ImGui_ImplWin32_EnableDpiAwareness;
using ::ImGui_ImplWin32_GetDpiScaleForHwnd;
using ::ImGui_ImplWin32_GetDpiScaleForMonitor;

using ::ImGui_ImplWin32_EnableAlphaCompositing;
#pragma endregion

}
