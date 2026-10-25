#include "state_serializer.h"

#include "model_serialization.h"

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <fstream>
#include <stdexcept>

namespace serialization {

StateSerializer::StateSerializer(model::Game& game,
                                 std::filesystem::path state_file,
                                 std::optional<std::chrono::milliseconds> save_period)
    : game_{game}
    , state_file_{std::move(state_file)}
    , save_period_{save_period} {
}

void StateSerializer::Restore() {
    if (!std::filesystem::exists(state_file_)) {
        return;
    }

    std::ifstream input{state_file_};
    if (!input) {
        throw std::runtime_error("Failed to open state file for reading: " + state_file_.string());
    }

    boost::archive::text_iarchive archive{input};
    GameStateRepr state;
    archive >> state;
    state.Restore(game_);
}

void StateSerializer::Save() const {
    const auto temp_file = std::filesystem::path{state_file_.string() + ".tmp"};
    {
        std::ofstream output{temp_file, std::ios::trunc};
        if (!output) {
            throw std::runtime_error("Failed to open temporary state file: " + temp_file.string());
        }
        boost::archive::text_oarchive archive{output};
        archive << GameStateRepr{game_};
        output.flush();
        if (!output) {
            throw std::runtime_error("Failed to write state file: " + temp_file.string());
        }
    }

    std::filesystem::rename(temp_file, state_file_);
}

void StateSerializer::OnTick(std::chrono::milliseconds delta) {
    if (!save_period_) {
        return;
    }
    time_since_save_ += delta;
    if (time_since_save_ >= *save_period_) {
        Save();
        time_since_save_ = std::chrono::milliseconds{0};
    }
}

}  // namespace serialization
