module GW2Viewer.Data.Content;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Encoding;
import std;

namespace GW2Viewer::Data::Content
{

std::wstring ContentTypeInfo::GetDisplayName() const
{
    auto const itr = G::Config.TypeInfo.find(Index);
    return itr != G::Config.TypeInfo.end() && !itr->second.Name.empty()
        ? Utils::Encoding::ToWString(itr->second.Name)
        : std::format(L"#{}", Index);
}

}
