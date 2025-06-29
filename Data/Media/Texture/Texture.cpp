module;
#include <d3d11.h>

module GW2Viewer.Data.Media.Texture;

namespace Data::Media::Texture
{

Texture::~Texture()
{
    if (Handle)
        ((ID3D11ShaderResourceView*)Handle)->Release();
}

}
