#pragma once

#include "http_server.h"
#include "model.h"
#include "application.h"
#include "extra_data.h"
#include "records_repository.h"

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace http_handler {

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;

using namespace std::literals;

namespace endpoints {
inline constexpr std::string_view JOIN = "/api/v1/game/join"sv;
inline constexpr std::string_view PLAYERS = "/api/v1/game/players"sv;
inline constexpr std::string_view STATE = "/api/v1/game/state"sv;
inline constexpr std::string_view ACTION = "/api/v1/game/player/action"sv;
inline constexpr std::string_view TICK = "/api/v1/game/tick"sv;
inline constexpr std::string_view RECORDS = "/api/v1/game/records"sv;
inline constexpr std::string_view MAPS = "/api/v1/maps"sv;
inline constexpr std::string_view MAPS_PREFIX = "/api/v1/maps/"sv;
}  // namespace endpoints

class RequestHandler {
public:
    RequestHandler(app::Application& application, const extra_data::ExtraData& extra_data, records::Repository& records)
        : application_{application}
        , game_{application.GetGame()}
        , extra_data_{extra_data}
        , records_{records} {
    }

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Body, typename Allocator, typename Send>
    void operator()(
        http::request<Body, http::basic_fields<Allocator>>&& request,
        Send&& send
    ) {
        send(HandleRequest(std::move(request)));
    }

private:
    using StringRequest = http::request<http::string_body>;
    using StringResponse = http::response<http::string_body>;

    StringResponse HandleRequest(StringRequest&& request) {
        const std::string target{request.target()};

        if (target == endpoints::RECORDS || target.starts_with(std::string(endpoints::RECORDS) + "?")) {
            return HandleRecordsRequest(request);
        }

        if (target == endpoints::JOIN) {
            return HandleJoinGameRequest(request);
        }

        if (target == endpoints::PLAYERS) {
            return HandlePlayersRequest(request);
        }

        if (target == endpoints::STATE) {
            return HandleGameStateRequest(request);
        }

        if (target == endpoints::ACTION) {
            return HandlePlayerActionRequest(request);
        }

        if (target == endpoints::TICK) {
            return HandleTickRequest(request);
        }

        if (target == endpoints::MAPS) {
            if (request.method() != http::verb::get
                && request.method() != http::verb::head) {
                return MakeMethodNotAllowedResponse(
                    "GET, HEAD"sv,
                    request.version(),
                    request.keep_alive()
                );
            }

            return MakeJsonResponse(
                http::status::ok,
                SerializeMaps(),
                request.version(),
                request.keep_alive(),
                request.method() == http::verb::head
            );
        }

        const std::string_view maps_prefix = endpoints::MAPS_PREFIX;

        if (target.starts_with(maps_prefix)) {
            if (request.method() != http::verb::get
                && request.method() != http::verb::head) {
                return MakeMethodNotAllowedResponse(
                    "GET, HEAD"sv,
                    request.version(),
                    request.keep_alive()
                );
            }

            const std::string map_id = target.substr(maps_prefix.size());

            if (const model::Map* map =
                    game_.FindMap(model::Map::Id{map_id})) {
                return MakeJsonResponse(
                    http::status::ok,
                    SerializeMap(*map),
                    request.version(),
                    request.keep_alive(),
                    request.method() == http::verb::head
                );
            }

            return MakeErrorResponse(
                http::status::not_found,
                "mapNotFound"sv,
                "Map not found"sv,
                request.version(),
                request.keep_alive()
            );
        }

        return MakeErrorResponse(
            http::status::bad_request,
            "badRequest"sv,
            "Bad request"sv,
            request.version(),
            request.keep_alive()
        );
    }

    StringResponse HandleRecordsRequest(const StringRequest& request) {
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            return MakeMethodNotAllowedResponse("GET, HEAD"sv, request.version(), request.keep_alive());
        }
        try {
            std::size_t start = 0;
            std::size_t max_items = 100;
            const std::string target{request.target()};
            const auto qpos = target.find('?');
            if (qpos != std::string::npos) {
                std::string_view query{target.data() + qpos + 1, target.size() - qpos - 1};
                while (!query.empty()) {
                    const auto amp = query.find('&');
                    const auto part = query.substr(0, amp);
                    const auto eq = part.find('=');
                    if (eq != std::string_view::npos) {
                        const auto key = part.substr(0, eq);
                        const auto value = part.substr(eq + 1);
                        if (value.empty() || !std::all_of(value.begin(), value.end(), [](char c){ return std::isdigit(static_cast<unsigned char>(c)); })) {
                            throw std::invalid_argument("invalid query parameter");
                        }
                        const auto parsed = static_cast<std::size_t>(std::stoull(std::string(value)));
                        if (key == "start") start = parsed;
                        else if (key == "maxItems") max_items = parsed;
                    }
                    if (amp == std::string_view::npos) break;
                    query.remove_prefix(amp + 1);
                }
            }
            if (max_items > 100) {
                return MakeErrorResponse(http::status::bad_request, "badRequest"sv,
                    "maxItems must not exceed 100"sv, request.version(), request.keep_alive());
            }
            json::array body;
            for (const auto& item : records_.Load(start, max_items)) {
                body.emplace_back(json::object{{"name", item.name}, {"score", item.score}, {"playTime", item.play_time}});
            }
            return MakeJsonResponse(http::status::ok, json::serialize(body), request.version(),
                                    request.keep_alive(), request.method() == http::verb::head);
        } catch (const std::exception&) {
            return MakeErrorResponse(http::status::bad_request, "badRequest"sv,
                "Invalid request parameters"sv, request.version(), request.keep_alive());
        }
    }

    StringResponse HandleJoinGameRequest(const StringRequest& request) {
        if (request.method() != http::verb::post) {
            StringResponse response = MakeErrorResponse(
                http::status::method_not_allowed,
                "invalidMethod"sv,
                "Only POST method is expected"sv,
                request.version(),
                request.keep_alive()
            );

            response.set(http::field::allow, "POST");
            return response;
        }

        try {
            const json::value parsed = json::parse(request.body());
            const json::object& body = parsed.as_object();

            const std::string user_name =
                json::value_to<std::string>(body.at("userName"));

            const std::string map_id =
                json::value_to<std::string>(body.at("mapId"));

            if (user_name.empty()) {
                return MakeErrorResponse(
                    http::status::bad_request,
                    "invalidArgument"sv,
                    "Invalid name"sv,
                    request.version(),
                    request.keep_alive()
                );
            }

            const model::Map::Id typed_map_id{map_id};

            if (game_.FindMap(typed_map_id) == nullptr) {
                return MakeErrorResponse(
                    http::status::not_found,
                    "mapNotFound"sv,
                    "Map not found"sv,
                    request.version(),
                    request.keep_alive()
                );
            }

            model::GameSession& session =
                game_.GetOrCreateGameSession(typed_map_id);

            model::Dog& dog = session.AddDog(user_name);

            model::Player& player =
                game_.GetPlayers().Add(dog, session);

            const model::Token token =
                game_.GetPlayerTokens().AddPlayer(player);

            json::object response_body{
                {"authToken", *token},
                {"playerId", player.GetId()}
            };

            return MakeJsonResponse(
                http::status::ok,
                json::serialize(response_body),
                request.version(),
                request.keep_alive()
            );
        } catch (const std::exception&) {
            return MakeErrorResponse(
                http::status::bad_request,
                "invalidArgument"sv,
                "Join game request parse error"sv,
                request.version(),
                request.keep_alive()
            );
        }
    }

    StringResponse HandlePlayersRequest(const StringRequest& request) {
        if (request.method() != http::verb::get
            && request.method() != http::verb::head) {
            StringResponse response = MakeErrorResponse(
                http::status::method_not_allowed,
                "invalidMethod"sv,
                "Invalid method"sv,
                request.version(),
                request.keep_alive()
            );

            response.set(http::field::allow, "GET, HEAD");
            return response;
        }

        const auto token_result = Authorize(request);

        if (!token_result.player) {
            return std::move(token_result.error_response);
        }

        json::object players_json;

        for (const model::Player& player :
             game_.GetPlayers().GetPlayers()) {
            if (player.IsRetired() || &player.GetSession() != &token_result.player->GetSession()) {
                continue;
            }

            players_json[std::to_string(player.GetId())] = json::object{
                {"name", player.GetDog().GetName()}
            };
        }

        return MakeJsonResponse(
            http::status::ok,
            json::serialize(players_json),
            request.version(),
            request.keep_alive(),
            request.method() == http::verb::head
        );
    }

    StringResponse HandleGameStateRequest(const StringRequest& request) {
        if (request.method() != http::verb::get
            && request.method() != http::verb::head) {
            StringResponse response = MakeErrorResponse(
                http::status::method_not_allowed,
                "invalidMethod"sv,
                "Invalid method"sv,
                request.version(),
                request.keep_alive()
            );

            response.set(http::field::allow, "GET, HEAD");
            return response;
        }

        const auto token_result = Authorize(request);

        if (!token_result.player) {
            return std::move(token_result.error_response);
        }

        return MakeJsonResponse(
            http::status::ok,
            SerializeGameState(*token_result.player),
            request.version(),
            request.keep_alive(),
            request.method() == http::verb::head
        );
    }

    StringResponse HandlePlayerActionRequest(const StringRequest& request) {
        if (request.method() != http::verb::post) {
            StringResponse response = MakeErrorResponse(
                http::status::method_not_allowed,
                "invalidMethod"sv,
                "Invalid method"sv,
                request.version(),
                request.keep_alive()
            );

            response.set(http::field::allow, "POST");
            return response;
        }

        auto token_result = Authorize(request);

        if (!token_result.player) {
            return std::move(token_result.error_response);
        }

        const auto content_type = request.find(http::field::content_type);

        if (content_type == request.end()
            || content_type->value() != "application/json"sv) {
            return MakeErrorResponse(
                http::status::bad_request,
                "invalidArgument"sv,
                "Invalid content type"sv,
                request.version(),
                request.keep_alive()
            );
        }

        try {
            const json::value parsed = json::parse(request.body());
            const json::object& body = parsed.as_object();
            const std::string move =
                json::value_to<std::string>(body.at("move"));

            model::Dog& dog = token_result.player->GetDog();
            const double speed =
                token_result.player->GetSession().GetMap().GetDogSpeed();

            token_result.player->MarkActive();

            if (move == "L") {
                dog.SetDirection(model::Direction::WEST);
                dog.SetSpeed({-speed, 0.0});
            } else if (move == "R") {
                dog.SetDirection(model::Direction::EAST);
                dog.SetSpeed({speed, 0.0});
            } else if (move == "U") {
                dog.SetDirection(model::Direction::NORTH);
                dog.SetSpeed({0.0, -speed});
            } else if (move == "D") {
                dog.SetDirection(model::Direction::SOUTH);
                dog.SetSpeed({0.0, speed});
            } else if (move.empty()) {
                dog.SetSpeed({0.0, 0.0});
            } else {
                throw std::invalid_argument("Invalid move");
            }

            return MakeJsonResponse(
                http::status::ok,
                "{}",
                request.version(),
                request.keep_alive()
            );
        } catch (const std::exception&) {
            return MakeErrorResponse(
                http::status::bad_request,
                "invalidArgument"sv,
                "Failed to parse action"sv,
                request.version(),
                request.keep_alive()
            );
        }
    }


    StringResponse HandleTickRequest(const StringRequest& request) {
        if (request.method() != http::verb::post) {
            StringResponse response = MakeErrorResponse(
                http::status::method_not_allowed,
                "invalidMethod"sv,
                "Invalid method"sv,
                request.version(),
                request.keep_alive());
            response.set(http::field::allow, "POST");
            return response;
        }

        const auto ct = request.find(http::field::content_type);
        if (ct == request.end() || ct->value() != "application/json"sv) {
            return MakeErrorResponse(http::status::bad_request,
                "invalidArgument"sv,
                "Invalid content type"sv,
                request.version(),
                request.keep_alive());
        }

        try {
            auto obj = json::parse(request.body()).as_object();
            const auto& v = obj.at("timeDelta");
            std::uint64_t dt;
            if (v.is_uint64()) {
                dt = v.as_uint64();
            } else if (v.is_int64()) {
                auto s = v.as_int64();
                if (s < 0) throw std::invalid_argument("negative");
                dt = static_cast<std::uint64_t>(s);
            } else {
                throw std::invalid_argument("invalid type");
            }
            application_.Tick(std::chrono::milliseconds{dt});
            return MakeJsonResponse(http::status::ok,"{}",request.version(),request.keep_alive());
        } catch (const std::exception&) {
            return MakeErrorResponse(http::status::bad_request,
                "invalidArgument"sv,
                "Failed to parse tick request JSON"sv,
                request.version(),
                request.keep_alive());
        }
    }

    struct AuthorizationResult {
        model::Player* player = nullptr;
        StringResponse error_response;
    };

    AuthorizationResult Authorize(const StringRequest& request) {
        const auto authorization = request.find(http::field::authorization);

        if (authorization == request.end()) {
            return {
                nullptr,
                MakeErrorResponse(
                    http::status::unauthorized,
                    "invalidToken"sv,
                    "Authorization header is required"sv,
                    request.version(),
                    request.keep_alive()
                )
            };
        }

        const std::string authorization_value{
            authorization->value().data(),
            authorization->value().size()
        };

        const auto token = ExtractToken(authorization_value);

        if (!token.has_value()) {
            return {
                nullptr,
                MakeErrorResponse(
                    http::status::unauthorized,
                    "invalidToken"sv,
                    "Authorization header is invalid"sv,
                    request.version(),
                    request.keep_alive()
                )
            };
        }

        model::Player* player =
            game_.GetPlayerTokens().FindPlayerByToken(*token);

        if (player == nullptr) {
            return {
                nullptr,
                MakeErrorResponse(
                    http::status::unauthorized,
                    "unknownToken"sv,
                    "Player token has not been found"sv,
                    request.version(),
                    request.keep_alive()
                )
            };
        }

        return {player, {}};
    }

    static std::optional<model::Token> ExtractToken(
        std::string_view authorization
    ) {
        constexpr std::string_view bearer_prefix = "Bearer "sv;

        if (!authorization.starts_with(bearer_prefix)) {
            return std::nullopt;
        }

        const std::string_view token =
            authorization.substr(bearer_prefix.size());

        if (token.size() != 32) {
            return std::nullopt;
        }

        const bool is_hex = std::all_of(
            token.begin(),
            token.end(),
            [](unsigned char character) {
                return std::isxdigit(character) != 0;
            }
        );

        if (!is_hex) {
            return std::nullopt;
        }

        return model::Token{std::string(token)};
    }

    static std::string_view DirectionToString(
        model::Direction direction
    ) {
        switch (direction) {
        case model::Direction::NORTH:
            return "U"sv;
        case model::Direction::SOUTH:
            return "D"sv;
        case model::Direction::WEST:
            return "L"sv;
        case model::Direction::EAST:
            return "R"sv;
        }

        return "U"sv;
    }

    std::string SerializeGameState(
        const model::Player& current_player
    ) const {
        json::object players_json;

        for (const model::Player& player :
             game_.GetPlayers().GetPlayers()) {
            if (player.IsRetired() || &player.GetSession() != &current_player.GetSession()) {
                continue;
            }

            const model::Dog& dog = player.GetDog();
            const model::Position position = dog.GetPosition();
            const model::Speed speed = dog.GetSpeed();

            json::array bag_json;
            for (const auto& item : dog.GetBag()) {
                bag_json.emplace_back(json::object{
                    {"id", item.id},
                    {"type", item.type}
                });
            }

            players_json[std::to_string(player.GetId())] = json::object{
                {"pos", json::array{position.x, position.y}},
                {"speed", json::array{speed.x, speed.y}},
                {"dir", DirectionToString(dog.GetDirection())},
                {"bag", std::move(bag_json)},
                {"score", dog.GetScore()}
            };
        }

        json::object lost_objects_json;
        for (const auto& object : current_player.GetSession().GetLostObjects()) {
            lost_objects_json[std::to_string(object.id)] = json::object{
                {"type", object.type},
                {"pos", json::array{object.position.x, object.position.y}}
            };
        }
        json::object response_body{
            {"players", std::move(players_json)},
            {"lostObjects", std::move(lost_objects_json)}
        };

        return json::serialize(response_body);
    }

    StringResponse MakeJsonResponse(
        http::status status,
        std::string body,
        unsigned version,
        bool keep_alive,
        bool head_request = false
    ) const {
        StringResponse response{status, version};

        response.set(http::field::content_type, "application/json");
        response.set(http::field::cache_control, "no-cache");
        response.keep_alive(keep_alive);

        if (head_request) {
            response.content_length(body.size());
        } else {
            response.body() = std::move(body);
            response.content_length(response.body().size());
        }

        return response;
    }

    StringResponse MakeErrorResponse(
        http::status status,
        std::string_view code,
        std::string_view message,
        unsigned version,
        bool keep_alive
    ) const {
        json::object body{
            {"code", code},
            {"message", message}
        };

        return MakeJsonResponse(
            status,
            json::serialize(body),
            version,
            keep_alive
        );
    }

    StringResponse MakeMethodNotAllowedResponse(
        std::string_view allowed_methods,
        unsigned version,
        bool keep_alive
    ) const {
        StringResponse response = MakeErrorResponse(
            http::status::method_not_allowed,
            "invalidMethod"sv,
            "Invalid method"sv,
            version,
            keep_alive
        );

        response.set(http::field::allow, allowed_methods);
        return response;
    }

    std::string SerializeMaps() const {
        json::array maps_json;

        for (const auto& map : game_.GetMaps()) {
            maps_json.emplace_back(json::object{
                {"id", *map.GetId()},
                {"name", map.GetName()}
            });
        }

        return json::serialize(maps_json);
    }

    static json::array SerializeRoads(const model::Map& map) {
        json::array roads;
        for (const auto& road : map.GetRoads()) {
            const model::Point start = road.GetStart();
            const model::Point end = road.GetEnd();
            json::object road_json{{"x0", start.x}, {"y0", start.y}};
            road_json[road.IsHorizontal() ? "x1" : "y1"] =
                road.IsHorizontal() ? end.x : end.y;
            roads.emplace_back(std::move(road_json));
        }
        return roads;
    }

    static json::array SerializeBuildings(const model::Map& map) {
        json::array buildings;
        for (const auto& building : map.GetBuildings()) {
            const auto& bounds = building.GetBounds();
            buildings.emplace_back(json::object{
                {"x", bounds.position.x}, {"y", bounds.position.y},
                {"w", bounds.size.width}, {"h", bounds.size.height}
            });
        }
        return buildings;
    }

    static json::array SerializeOffices(const model::Map& map) {
        json::array offices;
        for (const auto& office : map.GetOffices()) {
            const auto position = office.GetPosition();
            const auto offset = office.GetOffset();
            offices.emplace_back(json::object{
                {"id", *office.GetId()}, {"x", position.x}, {"y", position.y},
                {"offsetX", offset.dx}, {"offsetY", offset.dy}
            });
        }
        return offices;
    }

    std::string SerializeMap(const model::Map& map) const {
        json::object map_json{
            {"id", *map.GetId()},
            {"name", map.GetName()},
            {"roads", SerializeRoads(map)},
            {"buildings", SerializeBuildings(map)},
            {"offices", SerializeOffices(map)},
            {"lootTypes", extra_data_.GetLootTypes(map.GetId())}
        };
        return json::serialize(map_json);
    }

private:
    app::Application& application_;
    model::Game& game_;
    const extra_data::ExtraData& extra_data_;
    records::Repository& records_;
};

}  // namespace http_handler
