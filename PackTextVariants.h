#pragma once
#include "Common.h"

#include <boost/container/static_vector.hpp>

#include <unordered_map>

extern std::unordered_map<uint32, boost::container::static_vector<uint32, (size_t)Race::Max * (size_t)Sex::None>> g_textVariants;

void LoadTextPackVariants(class Archive& archive, class ProgressBarContext& progress);
