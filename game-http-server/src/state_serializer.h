#pragma once

#include "model.h"

#include <chrono>
#include <filesystem>
#include <optional>

namespace serialization {

class StateSerializer {
public:
    StateSerializer(model::Game& game,
                    std::filesystem::path state_file,
                    std::optional<std::chrono::milliseconds> save_period);

    void Restore();
    void Save() const;
    void OnTick(std::chrono::milliseconds delta);

private:
    model::Game& game_;
    std::filesystem::path state_file_;
    std::optional<std::chrono::milliseconds> save_period_;
    std::chrono::milliseconds time_since_save_{0};
};

}  // namespace serialization
