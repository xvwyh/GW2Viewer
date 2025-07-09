#include "Resource.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <d3d11.h>
//#define DIRECTINPUT_VERSION 0x0800
//#include <dinput.h>
#include <tchar.h>

import GW2Viewer.Data.Game;
import GW2Viewer.System.Graphics;
import GW2Viewer.UI.Manager;
import GW2Viewer.User.Config;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    using namespace GW2Viewer;

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (G::GraphicsDevice && wParam != SIZE_MINIMIZED)
        {
            if (G::RenderTargetView) { G::RenderTargetView->Release(); G::RenderTargetView = nullptr; }
            G::SwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* pBackBuffer;
            G::SwapChain->GetBuffer(0, (IID&)IID_PPV_ARGS(&pBackBuffer));
            G::GraphicsDevice->CreateRenderTargetView(pBackBuffer, nullptr, &G::RenderTargetView);
            pBackBuffer->Release();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    using namespace GW2Viewer;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Create application window
    WNDCLASSEX wc =
    {
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_CLASSDC,
        .lpfnWndProc = WndProc,
        .cbClsExtra = 0,
        .cbWndExtra = 0,
        .hInstance = hInstance,
        .hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON)),
        .hCursor = LoadCursor(nullptr, IDC_ARROW),
        .hbrBackground = nullptr,
        .lpszMenuName = nullptr,
        .lpszClassName = _T("GW2Viewer Window"),
        .hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON)),
    };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(_T("GW2Viewer Window"), _T("GW2Viewer"), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &G::SwapChain, &G::GraphicsDevice, &featureLevel, &G::GraphicsDeviceContext) != S_OK)
        return false;

    ID3D11Texture2D* pBackBuffer;
    G::SwapChain->GetBuffer(0, (IID&)IID_PPV_ARGS(&pBackBuffer));
    G::GraphicsDevice->CreateRenderTargetView(pBackBuffer, NULL, &G::RenderTargetView);
    pBackBuffer->Release();

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup Dear ImGui style
    //ImGui::StyleColorsDark();
    ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(G::GraphicsDevice, G::GraphicsDeviceContext);

    G::Config.Load();
    G::UI.Load();

    ImVec4 clear_col = ImColor(0, 0, 0);

    // Main loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        G::UI.Update();

        // Rendering
        ImGui::Render();
        G::GraphicsDeviceContext->OMSetRenderTargets(1, &G::RenderTargetView, NULL);
        G::GraphicsDeviceContext->ClearRenderTargetView(G::RenderTargetView, (float*)&clear_col);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        G::SwapChain->Present(1, 0); // Present with vsync
        //g_pSwapChain->Present(0, 0); // Present without vsync

        if (GetForegroundWindow() != hwnd && !ImGui::GetIO().WantCaptureMouse)
            Sleep(100);
    }

    G::UI.Unload();
    G::Config.Save();

    G::Game.Texture.StopLoading();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (G::RenderTargetView) { G::RenderTargetView->Release(); G::RenderTargetView = nullptr; }
    if (G::SwapChain) { G::SwapChain->Release(); G::SwapChain = nullptr; }
    if (G::GraphicsDeviceContext) { G::GraphicsDeviceContext->Release(); G::GraphicsDeviceContext = nullptr; }
    if (G::GraphicsDevice) try { G::GraphicsDevice->Release(); G::GraphicsDevice = nullptr; } catch (...) { }
    DestroyWindow(hwnd);
    UnregisterClass(_T("GW2Viewer Window"), wc.hInstance);

    CoUninitialize();

    return 0;
}
