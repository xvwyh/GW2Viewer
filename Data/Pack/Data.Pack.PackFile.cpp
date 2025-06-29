module GW2Viewer.Data.Pack.PackFile;
import :Traversal;
import GW2Viewer.Data.Game;
import std;

Data::Pack::Layout::Traversal::QueryChunk Data::Pack::PackFile::QueryChunk(fcc magic) const { return { *this, GetChunk(magic) }; }

std::map<uint32, Data::Pack::Layout::Type const*> const* GetChunk(std::string_view name) { return G::Game.Pack.GetChunk(name); }
