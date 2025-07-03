module GW2Viewer.User.Config;
import GW2Viewer.Common.JSON;

static std::filesystem::path const path = R"(.\config.json)";

namespace GW2Viewer::User
{

void Config::Load()
{
    auto contents = (std::stringstream() << std::ifstream(path).rdbuf()).str();
    if (contents.empty())
        return;
    from_json(json::parse(contents, nullptr, false), *this);
    FinishLoading();
}
void Config::Save()
{
    std::erase_if(ContentNamespaceNames, [](auto const& pair) { return pair.second.empty(); });
    std::erase_if(ContentObjectNames, [](auto const& pair) { return pair.second.empty(); });
    std::ofstream(path) << ordered_json(*this).dump(2);
}

}
