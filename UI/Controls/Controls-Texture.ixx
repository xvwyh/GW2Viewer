module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Controls:Texture;
import GW2Viewer.Common;
import GW2Viewer.Data.Game;
import GW2Viewer.Data.Media.Texture;

export namespace UI::Controls
{

struct TextureOptions
{
    std::vector<byte> const* Data;
    ImVec4 Color { 1, 1, 1, 1 };
    ImVec2 Size { };
    ImRect UV { 0, 0, 1, 1 };
    std::optional<ImRect> UV2;
    bool PreserveAspectRatio = true;
    bool FullPreviewOnHover = true;
    bool AdvanceCursor = true;
    bool ReserveSpace = false;
};
bool Texture(uint32 textureFileID, TextureOptions const& options = { })
{
    if (auto const texture = G::Game.Texture.Get(textureFileID); !texture || texture->TextureLoadingState == Data::Media::Texture::TextureEntry::TextureLoadingStates::NotLoaded)
        G::Game.Texture.Load(textureFileID, { .DataSource = options.Data });
    else if (texture && texture->Texture && texture->Texture->Handle)
    {
        ImVec2 const fullSize { (float)texture->Texture->Width, (float)texture->Texture->Height };
        ImVec2 offset { };
        ImVec2 size = options.Size;
        if (!size.x && !size.y)
            size = fullSize;
        else if (!size.x)
            size.x = options.PreserveAspectRatio ? size.y * (fullSize.x / fullSize.y) : size.y;
        else if (!size.y)
            size.y = options.PreserveAspectRatio ? size.x * (fullSize.y / fullSize.x) : size.x;
        else if (options.PreserveAspectRatio)
            offset = (options.Size - (size = fullSize * std::min(size.x / fullSize.x, size.y / fullSize.y))) * 0.5f;
        auto const pos = I::GetCursorScreenPos();
        ImRect const bb { pos, pos + size + offset * 2 };
        if (options.AdvanceCursor)
            I::ItemSize(bb);
        I::ItemAdd(bb, 0);
        if (auto& draw = *I::GetWindowDrawList(); options.UV2 && options.Color.w > 0)
        {
            const bool push_texture_id = texture->Texture->Handle != draw._CmdHeader.TextureId;
            if (push_texture_id)
                draw.PushTextureID(texture->Texture->Handle);

            draw.PrimReserve(6, 4);
            draw.PrimRectUV(bb.Min + offset, bb.Max - offset, options.UV.Min, options.UV.Max, I::ColorConvertFloat4ToU32(options.Color));
            draw._VtxWritePtr[-4].uv2 = options.UV2->GetTL();
            draw._VtxWritePtr[-3].uv2 = options.UV2->GetTR();
            draw._VtxWritePtr[-2].uv2 = options.UV2->GetBR();
            draw._VtxWritePtr[-1].uv2 = options.UV2->GetBL();

            if (push_texture_id)
                draw.PopTextureID();
        }
        else
            draw.AddImage(texture->Texture->Handle, bb.Min + offset, bb.Max - offset, options.UV.Min, options.UV.Max, I::ColorConvertFloat4ToU32(options.Color));
        if (options.FullPreviewOnHover && size != fullSize)
            if (scoped::ItemTooltip(ImGuiHoveredFlags_DelayNone))
                I::Image(texture->Texture->Handle, fullSize);
        return true;
    }
    if (options.ReserveSpace && (options.Size.x || options.Size.y))
    {
        auto const pos = I::GetCursorScreenPos();
        ImRect const bb { pos, pos + options.Size };
        if (options.AdvanceCursor)
            I::ItemSize(bb);
        I::ItemAdd(bb, 0);
    }
    return false;
}

}
