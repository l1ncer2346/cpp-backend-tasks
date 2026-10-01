#pragma once

#include <filesystem>

#include "extra_data.h"
#include "model.h"

namespace json_loader {

struct LoadedGame {
    model::Game game;
    extra_data::MapsExtra extra;
};

LoadedGame LoadGame(const std::filesystem::path& path);

}  // namespace json_loader
