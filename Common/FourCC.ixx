export module GW2Viewer.Common.FourCC;
import GW2Viewer.Common;

namespace GW2Viewer
{

consteval uint32 makefcc(char const (&magic)[4])
{
    return magic[0] << 0 | magic[1] << 8 | magic[2] << 16;
}
consteval uint32 makefcc(char const (&magic)[5])
{
    return magic[0] << 0 | magic[1] << 8 | magic[2] << 16 | magic[3] << 24;
}

#define FourCC(magic) magic = makefcc(#magic)
export enum class fcc : uint32
{
    Empty = 0,

    PF1 = 0x00014650,
    PF2 = 0x00024650,
    PF3 = 0x00034650,
    PF4 = 0x00044650,
    PF5 = 0x00054650,

    FourCC(ATEX),
    FourCC(ATTX),
    FourCC(ATEC),
    FourCC(ATEP),
    FourCC(ATEU),
    FourCC(ATET),
    _3DCX = makefcc("3DCX"),
    FourCC(DXT ),
    FourCC(DDS ),
    FourCC(strs),
    FourCC(asnd),
    FourCC(RIFF),
    TTF = 0x00000100,
    FourCC(OggS),
    FourCC(ARAP),
    FourCC(CTEX),

    // Texture codec
    FourCC(DXT1),
    FourCC(DXT2),
    FourCC(DXT3),
    FourCC(DXT4),
    FourCC(DXT5),
    FourCC(DXTN),
    FourCC(DXTL),
    FourCC(DXTA),
    FourCC(R32F),

    // RIFF FourCC
    FourCC(WEBP),

    // PF FourCC
    FourCC(ARMF),
    FourCC(ASND),
    FourCC(ABNK),
    FourCC(ABIX),
    FourCC(AMSP),
    FourCC(CDHS),
    FourCC(CINP),
    FourCC(cntc),
    FourCC(MODL),
    FourCC(GEOM),
    FourCC(DEPS),
    FourCC(EULA),
    FourCC(hvkC),
    FourCC(locl),
    FourCC(mapc),
    FourCC(mpsd),
    FourCC(PIMG),
    FourCC(PGTB),
    FourCC(AMAT),
    FourCC(anic),
    FourCC(emoc),
    FourCC(prlt),
    FourCC(cmpc),
    FourCC(txtm),
    FourCC(txtV),
    FourCC(txtv),
    PNG = 0x474e5089,
    FourCC(cmaC),
    FourCC(mMet),
    FourCC(AFNT),

    // PF Chunk FourCC
    FourCC(BKCK),
    FourCC(Main),
    FourCC(vari),
    FourCC(BIDX),
};
#undef FourCC

}
