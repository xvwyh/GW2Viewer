module GW2Viewer.Data.Pack.PackFile;
import :Traversal;
import GW2Viewer.Data.Game;
import std;

namespace GW2Viewer::Data::Pack
{

Layout::Traversal::QueryChunk PackFile::QueryChunk(fcc magic) const { return { *this, GetChunk(magic) }; }

}

namespace GW2Viewer::Data::Pack::Layout::Traversal
{

std::map<uint32, Type const*> const* GetChunk(std::string_view name) { return G::Game.Pack.GetChunk(name); }

}
