#include "Config.h"

#include <fstream>

Config g_config;

static std::filesystem::path const path = R"(.\config.json)";

void Config::Load()
{
    auto contents = (std::stringstream() << std::ifstream(path).rdbuf()).str();
    if (contents.empty())
        return;
    nlohmann::from_json(json::parse(contents, nullptr, false), *this);
    FinishLoading();
}

void Config::Save()
{
    std::erase_if(ContentNamespaceNames, [](auto const& pair) { return pair.second.empty(); });
    std::erase_if(ContentObjectNames, [](auto const& pair) { return pair.second.empty(); });
    std::ofstream(path) << nlohmann::ordered_json(*this).dump(2);
}
