export module GW2Viewer.System.Graphics;
import <d3d11.h>;

export namespace G
{
    ID3D11Device* GraphicsDevice = nullptr;
    IDXGISwapChain* SwapChain = nullptr;
    ID3D11DeviceContext* GraphicsDeviceContext = nullptr;
    ID3D11RenderTargetView* RenderTargetView = nullptr;
}
