#pragma once
#include <filesystem>
#include "model.h"
#include "extra_data.h"
namespace json_loader {
struct LoadedGame { model::Game game; extra_data::ExtraData extra_data; };
LoadedGame LoadGame(const std::filesystem::path& json_path);
}
