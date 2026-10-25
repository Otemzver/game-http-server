#include "ticker.h"

namespace app {

Ticker::Ticker(Strand strand, std::chrono::milliseconds period, Application& application)
    : strand_{std::move(strand)}
    , timer_{strand_}
    , period_{period}
    , application_{application}
    , last_tick_{std::chrono::steady_clock::now()} {
}

void Ticker::Start() {
    ScheduleTick();
}

void Ticker::ScheduleTick() {
    timer_.expires_after(period_);
    timer_.async_wait(
        [self = shared_from_this()](const boost::system::error_code& error) {
            self->OnTick(error);
        }
    );
}

void Ticker::OnTick(const boost::system::error_code& error) {
    if (error) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_tick_
    );
    last_tick_ = now;
    application_.Tick(delta);
    ScheduleTick();
}

}  // namespace app
