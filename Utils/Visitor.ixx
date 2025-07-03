export module GW2Viewer.Utils.Visitor;

export namespace GW2Viewer::Utils::Visitor
{

template<class... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };

}
