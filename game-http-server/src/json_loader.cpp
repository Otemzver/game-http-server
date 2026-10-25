#include "json_loader.h"

#include <boost/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace json_loader {

namespace json = boost::json;

namespace {

constexpr double MILLISECONDS_PER_SECOND = 1000.0;
constexpr double DEFAULT_DOG_SPEED = 1.0;
constexpr std::size_t DEFAULT_BAG_CAPACITY = 3;

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path);

    if (!file) {
        throw std::runtime_error("Failed to open config file: " + path.string());
    }

    std::ostringstream output;
    output << file.rdbuf();
    return output.str();
}

json::object ParseRoot(const std::filesystem::path& path) {
    try {
        return json::parse(ReadFile(path)).as_object();
    } catch (const std::exception& ex) {
        throw std::runtime_error(
            "Failed to parse config file '" + path.string() + "': " + ex.what()
        );
    }
}

int AsInt(const json::object& object, const char* key) {
    return static_cast<int>(object.at(key).as_int64());
}

double AsDouble(const json::object& object, const char* key) {
    const json::value& value = object.at(key);

    if (value.is_double()) {
        return value.as_double();
    }
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }

    throw std::runtime_error(std::string("Expected a numeric value for '") + key + "'");
}

std::string AsString(const json::object& object, const char* key) {
    return std::string(object.at(key).as_string());
}

std::size_t AsSize(const json::object& object, const char* key) {
    const auto value = object.at(key).to_number<std::uint64_t>();
    return static_cast<std::size_t>(value);
}

void LoadRoads(model::Map& map, const json::array& roads) {
    for (const json::value& road_value : roads) {
        const json::object& road = road_value.as_object();
        const model::Point start{AsInt(road, "x0"), AsInt(road, "y0")};

        if (road.contains("x1")) {
            map.AddRoad(model::Road{model::Road::HORIZONTAL, start, AsInt(road, "x1")});
        } else {
            map.AddRoad(model::Road{model::Road::VERTICAL, start, AsInt(road, "y1")});
        }
    }
}

void LoadBuildings(model::Map& map, const json::array& buildings) {
    for (const json::value& building_value : buildings) {
        const json::object& building = building_value.as_object();
        map.AddBuilding(model::Building{{
            {AsInt(building, "x"), AsInt(building, "y")},
            {AsInt(building, "w"), AsInt(building, "h")}
        }});
    }
}

void LoadOffices(model::Map& map, const json::array& offices) {
    for (const json::value& office_value : offices) {
        const json::object& office = office_value.as_object();
        map.AddOffice(model::Office{
            model::Office::Id{AsString(office, "id")},
            {AsInt(office, "x"), AsInt(office, "y")},
            {AsInt(office, "offsetX"), AsInt(office, "offsetY")}
        });
    }
}

std::vector<std::uint32_t> LoadLootValues(const json::array& loot_types) {
    std::vector<std::uint32_t> values;
    values.reserve(loot_types.size());

    for (const json::value& loot_type : loot_types) {
        const auto& object = loot_type.as_object();
        if (const auto* value = object.if_contains("value")) {
            values.push_back(value->to_number<std::uint32_t>());
        } else {
            // Older configuration files may not contain a loot score.
            // Treat such loot as zero-value instead of aborting server startup.
            values.push_back(0);
        }
    }

    return values;
}

model::Map LoadMap(const json::object& map_json,
                   double default_dog_speed,
                   std::size_t default_bag_capacity) {
    const json::array& loot_types = map_json.at("lootTypes").as_array();
    if (loot_types.empty()) {
        throw std::runtime_error("Map must contain at least one loot type");
    }

    const double dog_speed = map_json.contains("dogSpeed")
        ? AsDouble(map_json, "dogSpeed")
        : default_dog_speed;
    const std::size_t bag_capacity = map_json.contains("bagCapacity")
        ? AsSize(map_json, "bagCapacity")
        : default_bag_capacity;

    model::Map map{
        model::Map::Id{AsString(map_json, "id")},
        AsString(map_json, "name"),
        dog_speed,
        loot_types.size(),
        bag_capacity
    };

    map.SetLootValues(LoadLootValues(loot_types));
    LoadRoads(map, map_json.at("roads").as_array());
    LoadBuildings(map, map_json.at("buildings").as_array());
    LoadOffices(map, map_json.at("offices").as_array());
    return map;
}

void LoadLootGeneratorConfig(model::Game& game, const json::object& root) {
    const json::object& config = root.at("lootGeneratorConfig").as_object();
    const auto period = std::chrono::milliseconds{
        static_cast<std::int64_t>(AsDouble(config, "period") * MILLISECONDS_PER_SECOND)
    };
    const double probability = AsDouble(config, "probability");

    if (period.count() <= 0 || probability < 0.0 || probability > 1.0) {
        throw std::runtime_error("Invalid lootGeneratorConfig");
    }

    game.SetLootGeneratorConfig(period, probability);
}

}  // namespace

LoadedGame LoadGame(const std::filesystem::path& json_path) {
    LoadedGame result;
    const json::object root = ParseRoot(json_path);

    const double default_dog_speed = root.contains("defaultDogSpeed")
        ? AsDouble(root, "defaultDogSpeed")
        : DEFAULT_DOG_SPEED;
    const std::size_t default_bag_capacity = root.contains("defaultBagCapacity")
        ? AsSize(root, "defaultBagCapacity")
        : DEFAULT_BAG_CAPACITY;

    LoadLootGeneratorConfig(result.game, root);
    const double retirement_seconds = root.contains("dogRetirementTime")
        ? AsDouble(root, "dogRetirementTime")
        : 60.0;
    if (retirement_seconds < 0.0) {
        throw std::runtime_error("dogRetirementTime must be non-negative");
    }
    result.game.SetDogRetirementTime(std::chrono::milliseconds{
        static_cast<std::int64_t>(retirement_seconds * MILLISECONDS_PER_SECOND)});

    for (const json::value& map_value : root.at("maps").as_array()) {
        const json::object& map_json = map_value.as_object();
        const model::Map::Id map_id{AsString(map_json, "id")};
        result.extra_data.SetLootTypes(map_id, map_json.at("lootTypes").as_array());
        result.game.AddMap(LoadMap(map_json, default_dog_speed, default_bag_capacity));
    }

    return result;
}

}  // namespace json_loader
