module;
#include <d3d11.h>

module GW2Viewer.Data.Texture;

namespace GW2Viewer::Data::Texture
{

Texture::~Texture()
{
    if (Handle.GetTexID())
        ((ID3D11ShaderResourceView*)Handle.GetTexID())->Release();
}

}
