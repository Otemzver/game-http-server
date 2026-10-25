#include "sdk.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/program_options.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "application.h"
#include "json_loader.h"
#include "request_handler.h"
#include "records_repository.h"
#include "server_logging.h"
#include "state_serializer.h"
#include "ticker.h"

using namespace std::literals;

namespace net = boost::asio;
namespace json = boost::json;
namespace logging = boost::log;
namespace po = boost::program_options;

namespace {

struct Args {
    std::filesystem::path config_file;
    std::filesystem::path www_root;
    std::optional<int> tick_period;
    std::optional<std::filesystem::path> state_file;
    std::optional<std::int64_t> save_state_period;
};

[[nodiscard]] std::optional<Args> ParseCommandLine(
    int argc,
    const char* const argv[]
) {
    Args args;

    po::options_description description{"Allowed options"s};

    description.add_options()
        ("help,h", "Show help")
        (
            "config-file,c",
            po::value(&args.config_file)->value_name("file"),
            "Path to the game configuration file"
        )
        (
            "www-root,w",
            po::value(&args.www_root)->value_name("dir"),
            "Path to the static files directory"
        )
        (
            "tick-period,t",
            po::value<int>()->value_name("milliseconds"),
            "Automatically update game time with this period"
        )
        (
            "state-file",
            po::value<std::filesystem::path>()->value_name("file"),
            "Path to the file used to save and restore game state"
        )
        (
            "save-state-period",
            po::value<std::int64_t>()->value_name("milliseconds"),
            "Game-time period between automatic state saves"
        );

    po::variables_map variables;
    po::store(po::parse_command_line(argc, argv, description), variables);

    if (variables.contains("help")) {
        std::cout << description << std::endl;
        return std::nullopt;
    }

    po::notify(variables);

    if (!variables.contains("config-file")) {
        throw std::runtime_error(
            "The --config-file option has not been specified"
        );
    }

    if (!variables.contains("www-root")) {
        throw std::runtime_error(
            "The --www-root option has not been specified"
        );
    }

    if (variables.contains("tick-period")) {
        const int period = variables["tick-period"].as<int>();

        if (period <= 0) {
            throw std::runtime_error(
                "The --tick-period value must be positive"
            );
        }

        args.tick_period = period;
    }

    if (variables.contains("state-file")) {
        args.state_file = variables["state-file"].as<std::filesystem::path>();
    }

    if (variables.contains("save-state-period")) {
        const auto period = variables["save-state-period"].as<std::int64_t>();
        if (period < 0) {
            throw std::runtime_error(
                "The --save-state-period value must be non-negative"
            );
        }
        args.save_state_period = period;
    }

    return args;
}

// Запускает функцию fn на n потоках, включая текущий поток.
template <typename Fn>
void RunWorkers(unsigned n, const Fn& fn) {
    n = std::max(1u, n);

    std::vector<std::jthread> workers;
    workers.reserve(n - 1);

    while (--n) {
        workers.emplace_back(fn);
    }

    fn();
}

void LogServerStarted(
    const net::ip::address& address,
    net::ip::port_type port
) {
    json::object data{
        {"port", port},
        {"address", address.to_string()}
    };

    BOOST_LOG_TRIVIAL(info)
        << logging::add_value(
               server_logging::additional_data,
               json::value{std::move(data)}
           )
        << "server started"sv;
}

void LogServerExited(int code) {
    json::object data{
        {"code", code}
    };

    BOOST_LOG_TRIVIAL(info)
        << logging::add_value(
               server_logging::additional_data,
               json::value{std::move(data)}
           )
        << "server exited"sv;
}

void LogServerExited(int code, std::string_view exception) {
    json::object data{
        {"code", code},
        {"exception", exception}
    };

    BOOST_LOG_TRIVIAL(error)
        << logging::add_value(
               server_logging::additional_data,
               json::value{std::move(data)}
           )
        << "server exited"sv;
}

}  // namespace

int main(int argc, const char* argv[]) {
    try {
        const std::optional<Args> parsed_args =
            ParseCommandLine(argc, argv);

        if (!parsed_args) {
            return EXIT_SUCCESS;
        }

        const Args& args = *parsed_args;

        server_logging::InitLogging();

        auto loaded = json_loader::LoadGame(args.config_file);
        model::Game& game = loaded.game;
        app::Application application{game};

        const char* db_url = std::getenv("GAME_DB_URL");
        if (!db_url) {
            throw std::runtime_error("GAME_DB_URL is not specified");
        }
        records::Repository records_repository{db_url};
        boost::signals2::scoped_connection retire_connection = application.DoOnRetire(
            [&records_repository](const model::RetiredPlayerData& player) {
                records_repository.Save({player.name, player.score,
                    static_cast<double>(player.play_time_ms) / 1000.0});
            });

        std::unique_ptr<serialization::StateSerializer> state_serializer;
        boost::signals2::scoped_connection save_connection;

        if (args.state_file) {
            std::optional<std::chrono::milliseconds> save_period;
            if (args.save_state_period) {
                save_period = std::chrono::milliseconds{*args.save_state_period};
            }

            state_serializer = std::make_unique<serialization::StateSerializer>(
                game, *args.state_file, save_period);

            try {
                state_serializer->Restore();
            } catch (const std::exception& ex) {
                BOOST_LOG_TRIVIAL(error) << "Failed to restore game state: " << ex.what();
                throw;
            }

            save_connection = application.DoOnTick(
                [&state_serializer](std::chrono::milliseconds delta) {
                    state_serializer->OnTick(delta);
                });
        }

        const unsigned num_threads =
            std::max(1u, std::thread::hardware_concurrency());

        net::io_context ioc{static_cast<int>(num_threads)};
        auto api_strand = net::make_strand(ioc);

        net::signal_set signals{ioc, SIGINT, SIGTERM};

        signals.async_wait(
            [&ioc](
                const boost::system::error_code& error,
                [[maybe_unused]] int signal_number
            ) {
                if (!error) {
                    ioc.stop();
                }
            }
        );

        http_handler::RequestHandler handler{application, loaded.extra_data, records_repository};

        auto adapted_handler =
            [&handler, api_strand](
                [[maybe_unused]] auto&& endpoint,
                auto&& request,
                auto&& send
            ) mutable {
                net::dispatch(
                    api_strand,
                    [
                        &handler,
                        request = std::forward<decltype(request)>(request),
                        send = std::forward<decltype(send)>(send)
                    ]() mutable {
                        handler(std::move(request), std::move(send));
                    }
                );
            };

        server_logging::LoggingRequestHandler logging_handler{
            adapted_handler
        };

        std::shared_ptr<app::Ticker> ticker;

        if (args.tick_period) {
            ticker = std::make_shared<app::Ticker>(
                api_strand,
                std::chrono::milliseconds{*args.tick_period},
                application
            );

            ticker->Start();
        }

        // Путь к статическим файлам разобран и проверен.
        // Его подключение выполняется обработчиком статических файлов проекта.
        [[maybe_unused]] const auto& www_root = args.www_root;

        const auto address = net::ip::make_address("0.0.0.0");
        constexpr net::ip::port_type port = 8080;

        http_server::ServeHttp(
            ioc,
            {address, port},
            std::move(logging_handler)
        );

        LogServerStarted(address, port);

        RunWorkers(num_threads, [&ioc] {
            ioc.run();
        });

        if (state_serializer) {
            state_serializer->Save();
        }

        LogServerExited(EXIT_SUCCESS);
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        // При ошибке разбора командной строки логгер может быть ещё не настроен.
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
}
