module GW2Viewer.Data.Content;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Encoding;
import std;

namespace GW2Viewer::Data::Content
{

std::wstring ContentTypeInfo::GetDisplayName() const
{
    auto const& typeInfo = GetTypeInfo();
    return !typeInfo.Name.empty()
        ? Utils::Encoding::ToWString(typeInfo.Name)
        : std::format(L"#{}", Index);
}
TypeInfo& ContentTypeInfo::GetTypeInfo() const
{
    auto& typeInfo = G::Config.TypeInfo.try_emplace(Index).first->second;
    typeInfo.Initialize(*this);
    return typeInfo;
}

}
