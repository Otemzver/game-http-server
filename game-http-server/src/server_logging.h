#pragma once

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/json.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace server_logging {

    namespace json = boost::json;
    namespace logging = boost::log;
    namespace expr = boost::log::expressions;
    namespace keywords = boost::log::keywords;

    using namespace std::literals;

    BOOST_LOG_ATTRIBUTE_KEYWORD(
        timestamp,
        "TimeStamp",
        boost::posix_time::ptime
    )

        BOOST_LOG_ATTRIBUTE_KEYWORD(
            additional_data,
            "AdditionalData",
            json::value
        )

        inline void JsonFormatter(
            const logging::record_view& record,
            logging::formatting_ostream& stream
        ) {
        json::object result;

        if (const auto time = record[timestamp]) {
            result["timestamp"] = boost::posix_time::to_iso_extended_string(*time);
        }
        else {
            result["timestamp"] = nullptr;
        }

        if (const auto data = record[additional_data]) {
            result["data"] = *data;
        }
        else {
            result["data"] = json::object{};
        }

        if (const auto message = record[expr::smessage]) {
            result["message"] = message.get();
        }
        else {
            result["message"] = ""s;
        }

        stream << json::serialize(result);
    }

    inline void InitLogging() {
        logging::add_common_attributes();

        logging::add_console_log(
            std::cout,
            keywords::format = &JsonFormatter,
            keywords::auto_flush = true
        );
    }

    inline void LogError(
        const boost::system::error_code& error,
        std::string_view where
    ) {
        json::object data{
            {"code", error.value()},
            {"text", error.message()},
            {"where", where}
        };

        BOOST_LOG_TRIVIAL(error)
            << logging::add_value(additional_data, json::value{ std::move(data) })
            << "error"sv;
    }

    template <typename RequestHandler>
    class LoggingRequestHandler {
    public:
        explicit LoggingRequestHandler(RequestHandler request_handler)
            : request_handler_{ std::move(request_handler) } {
        }

        template <typename Body, typename Allocator, typename Send>
        void operator()(
            const boost::asio::ip::tcp::endpoint& endpoint,
            boost::beast::http::request<
            Body,
            boost::beast::http::basic_fields<Allocator>
            >&& request,
            Send&& send
            ) {
            const auto start_time = std::chrono::steady_clock::now();

            LogRequest(endpoint, request);

            request_handler_(
                endpoint,
                std::move(request),
                [
                    endpoint,
                    start_time,
                    send = std::forward<Send>(send)
                ](auto&& response) mutable {
                    LogResponse(endpoint, response, start_time);

                    send(std::forward<decltype(response)>(response));
                }
            );
        }

    private:
        template <typename Body, typename Allocator>
        static void LogRequest(
            const boost::asio::ip::tcp::endpoint& endpoint,
            const boost::beast::http::request<
            Body,
            boost::beast::http::basic_fields<Allocator>
            >& request
        ) {
            json::object data{
                {"ip", endpoint.address().to_string()},
                {"URI", std::string(request.target())},
                {"method", std::string(request.method_string())}
            };

            BOOST_LOG_TRIVIAL(info)
                << logging::add_value(
                    additional_data,
                    json::value{ std::move(data) }
                )
                << "request received"sv;
        }

        template <typename Body, typename Fields>
        static void LogResponse(
            const boost::asio::ip::tcp::endpoint& endpoint,
            const boost::beast::http::response<Body, Fields>& response,
            std::chrono::steady_clock::time_point start_time
        ) {
            const auto response_time =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time
                ).count();

            json::value content_type = nullptr;

            const auto content_type_it =
                response.find(boost::beast::http::field::content_type);

            if (content_type_it != response.end()) {
                content_type = std::string(content_type_it->value());
            }

            json::object data{
                {"ip", endpoint.address().to_string()},
                {"response_time", response_time},
                {"code", response.result_int()},
                {"content_type", std::move(content_type)}
            };

            BOOST_LOG_TRIVIAL(info)
                << logging::add_value(
                    additional_data,
                    json::value{ std::move(data) }
                )
                << "response sent"sv;
        }

    private:
        RequestHandler request_handler_;
    };

    template <typename RequestHandler>
    LoggingRequestHandler(RequestHandler) -> LoggingRequestHandler<RequestHandler>;

}  // namespace server_logging