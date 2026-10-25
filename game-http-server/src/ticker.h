#pragma once

#include "application.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <chrono>
#include <memory>
#include <utility>

namespace app {

class Ticker : public std::enable_shared_from_this<Ticker> {
public:
    using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;

    Ticker(Strand strand, std::chrono::milliseconds period, Application& application);

    void Start();

private:
    void ScheduleTick();
    void OnTick(const boost::system::error_code& error);

    Strand strand_;
    boost::asio::steady_timer timer_;
    std::chrono::milliseconds period_;
    Application& application_;
    std::chrono::steady_clock::time_point last_tick_;
};

}  // namespace app
