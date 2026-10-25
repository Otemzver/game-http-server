#pragma once

#include "model.h"

#include <boost/signals2.hpp>
#include <chrono>

namespace app {

class Application {
public:
    using TickSignal = boost::signals2::signal<void(std::chrono::milliseconds)>;
    using RetireSignal = boost::signals2::signal<void(const model::RetiredPlayerData&)>;

    explicit Application(model::Game& game)
        : game_{game} {
    }

    model::Game& GetGame() noexcept { return game_; }
    const model::Game& GetGame() const noexcept { return game_; }

    void Tick(std::chrono::milliseconds delta) {
        auto retired = game_.UpdateState(static_cast<std::uint64_t>(delta.count()));
        for (const auto& player : retired) {
            retire_signal_(player);
        }
        tick_signal_(delta);
    }

    [[nodiscard]] boost::signals2::connection DoOnTick(const TickSignal::slot_type& handler) {
        return tick_signal_.connect(handler);
    }

    [[nodiscard]] boost::signals2::connection DoOnRetire(const RetireSignal::slot_type& handler) {
        return retire_signal_.connect(handler);
    }

private:
    model::Game& game_;
    TickSignal tick_signal_;
    RetireSignal retire_signal_;
};

}  // namespace app
