#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <functional>

#include "tagged.h"
#include "loot_generator.h"
#include "collision_detector.h"

#include <chrono>

namespace model {

using Dimension = int;
using Coord = Dimension;

struct Point {
    Coord x;
    Coord y;
};

struct Size {
    Dimension width;
    Dimension height;
};

struct Rectangle {
    Point position;
    Size size;
};

struct Offset {
    Dimension dx;
    Dimension dy;
};

struct Position {
    double x = 0.0;
    double y = 0.0;
};

struct Speed {
    double x = 0.0;
    double y = 0.0;
};

enum class Direction {
    NORTH,
    SOUTH,
    WEST,
    EAST
};

class Road {
    struct HorizontalTag {
        explicit HorizontalTag() = default;
    };

    struct VerticalTag {
        explicit VerticalTag() = default;
    };

public:
    constexpr static HorizontalTag HORIZONTAL{};
    constexpr static VerticalTag VERTICAL{};

    Road(HorizontalTag, Point start, Coord end_x) noexcept
        : start_{start}
        , end_{end_x, start.y} {
    }

    Road(VerticalTag, Point start, Coord end_y) noexcept
        : start_{start}
        , end_{start.x, end_y} {
    }

    bool IsHorizontal() const noexcept {
        return start_.y == end_.y;
    }

    bool IsVertical() const noexcept {
        return start_.x == end_.x;
    }

    Point GetStart() const noexcept {
        return start_;
    }

    Point GetEnd() const noexcept {
        return end_;
    }

private:
    Point start_;
    Point end_;
};

class Building {
public:
    explicit Building(Rectangle bounds) noexcept
        : bounds_{bounds} {
    }

    const Rectangle& GetBounds() const noexcept {
        return bounds_;
    }

private:
    Rectangle bounds_;
};

class Office {
public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
        : id_{std::move(id)}
        , position_{position}
        , offset_{offset} {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    Point GetPosition() const noexcept {
        return position_;
    }

    Offset GetOffset() const noexcept {
        return offset_;
    }

private:
    Id id_;
    Point position_;
    Offset offset_;
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id, std::string name, double dog_speed = 1.0, std::size_t loot_types_count = 0, std::size_t bag_capacity = 3) noexcept
        : id_{std::move(id)}
        , name_{std::move(name)}
        , dog_speed_{dog_speed}
        , loot_types_count_{loot_types_count}
        , bag_capacity_{bag_capacity} {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    const Buildings& GetBuildings() const noexcept {
        return buildings_;
    }

    const Roads& GetRoads() const noexcept {
        return roads_;
    }

    const Offices& GetOffices() const noexcept {
        return offices_;
    }

    double GetDogSpeed() const noexcept {
        return dog_speed_;
    }

    std::size_t GetLootTypesCount() const noexcept {
        return loot_types_count_;
    }

    std::size_t GetBagCapacity() const noexcept {
        return bag_capacity_;
    }

    void SetLootValues(std::vector<std::uint32_t> values) {
        loot_values_ = std::move(values);
    }

    std::uint32_t GetLootValue(std::size_t type) const {
        return loot_values_.at(type);
    }

    void AddRoad(const Road& road) {
        roads_.emplace_back(road);
    }

    void AddBuilding(const Building& building) {
        buildings_.emplace_back(building);
    }

    void AddOffice(Office office);

private:
    using OfficeIdToIndex = std::unordered_map<
        Office::Id,
        std::size_t,
        util::TaggedHasher<Office::Id>
    >;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;
    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
    double dog_speed_ = 1.0;
    std::size_t loot_types_count_ = 0;
    std::size_t bag_capacity_ = 3;
    std::vector<std::uint32_t> loot_values_;
};

struct BagItem {
    std::uint32_t id = 0;
    std::size_t type = 0;
};

class Dog {
public:
    using Id = std::uint32_t;

    Dog(Id id, std::string name, Position position)
        : id_{id}
        , name_{std::move(name)}
        , position_{position} {
    }

    Id GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    Position GetPosition() const noexcept {
        return position_;
    }

    Speed GetSpeed() const noexcept {
        return speed_;
    }

    Direction GetDirection() const noexcept {
        return direction_;
    }

    void SetPosition(Position position) noexcept {
        position_ = position;
    }

    void SetSpeed(Speed speed) noexcept {
        speed_ = speed;
    }

    void SetDirection(Direction direction) noexcept {
        direction_ = direction;
    }

    const std::vector<BagItem>& GetBag() const noexcept { return bag_; }
    bool IsBagFull(std::size_t capacity) const noexcept { return bag_.size() >= capacity; }
    void AddToBag(BagItem item) { bag_.push_back(item); }
    void ClearBag() noexcept { bag_.clear(); }
    std::uint32_t GetScore() const noexcept { return score_; }
    void AddScore(std::uint32_t value) noexcept { score_ += value; }

private:
    Id id_;
    std::string name_;
    Position position_;
    Speed speed_{0.0, 0.0};
    Direction direction_ = Direction::NORTH;
    std::vector<BagItem> bag_;
    std::uint32_t score_ = 0;
};


struct LostObject {
    using Id = std::uint32_t;
    Id id = 0;
    std::size_t type = 0;
    Position position;
};

class GameSession {
public:
    using Dogs = std::deque<Dog>;
    using LostObjects = std::deque<LostObject>;

    GameSession(const Map& map, std::chrono::milliseconds loot_period, double loot_probability)
        : map_{&map}
        , loot_generator_{loot_period, loot_probability} {
    }

    Dog& AddDog(std::string name);

    const Map& GetMap() const noexcept {
        return *map_;
    }

    Dogs& GetDogs() noexcept {
        return dogs_;
    }

    const Dogs& GetDogs() const noexcept {
        return dogs_;
    }

    const LostObjects& GetLostObjects() const noexcept { return lost_objects_; }

    Dog& RestoreDog(Dog dog);
    void RestoreLostObject(LostObject object);
    Dog* FindDogById(Dog::Id id) noexcept;

    void UpdateState(std::chrono::milliseconds time_delta);

private:
    Position GetInitialPosition() const;
    void MoveDog(Dog& dog, double delta_seconds) const;
    Position GenerateRandomRoadPosition();
    void GenerateLoot(std::chrono::milliseconds time_delta);

private:
    const Map* map_;
    Dogs dogs_;
    Dog::Id next_dog_id_ = 0;
    LostObjects lost_objects_;
    LostObject::Id next_lost_object_id_ = 0;
    loot_gen::LootGenerator loot_generator_;
    std::mt19937 random_generator_{std::random_device{}()};
};

struct RetiredPlayerData {
    std::string name;
    std::uint32_t score = 0;
    std::uint64_t play_time_ms = 0;
};

class Player {
public:
    using Id = std::uint32_t;

    Player(Id id, Dog& dog, GameSession& session)
        : id_{id}
        , dog_{&dog}
        , session_{&session} {
    }

    Id GetId() const noexcept {
        return id_;
    }

    Dog& GetDog() noexcept {
        return *dog_;
    }

    const Dog& GetDog() const noexcept {
        return *dog_;
    }

    GameSession& GetSession() noexcept {
        return *session_;
    }

    const GameSession& GetSession() const noexcept {
        return *session_;
    }

    bool IsRetired() const noexcept { return retired_; }
    void Retire() noexcept { retired_ = true; }
    std::uint64_t GetPlayTimeMs() const noexcept { return play_time_ms_; }
    std::uint64_t GetIdleTimeMs() const noexcept { return idle_time_ms_; }
    void AddPlayTime(std::uint64_t delta) noexcept { play_time_ms_ += delta; }
    void AddIdleTime(std::uint64_t delta) noexcept { idle_time_ms_ += delta; }
    void ResetIdleTime() noexcept { idle_time_ms_ = 0; }
    void MarkActive() noexcept { idle_time_ms_ = 0; }

private:
    Id id_;
    Dog* dog_;
    GameSession* session_;
    std::uint64_t play_time_ms_ = 0;
    std::uint64_t idle_time_ms_ = 0;
    bool retired_ = false;
};

class Players {
public:
    using PlayerCollection = std::deque<Player>;

    Player& Add(Dog& dog, GameSession& session) {
        players_.emplace_back(next_player_id_++, dog, session);
        return players_.back();
    }

    PlayerCollection& GetPlayers() noexcept { return players_; }
    const PlayerCollection& GetPlayers() const noexcept { return players_; }

    Player& Restore(Player::Id id, Dog& dog, GameSession& session) {
        next_player_id_ = std::max(next_player_id_, static_cast<Player::Id>(id + 1));
        players_.emplace_back(id, dog, session);
        return players_.back();
    }

    void Clear() noexcept {
        players_.clear();
        next_player_id_ = 0;
    }

private:
    PlayerCollection players_;
    Player::Id next_player_id_ = 0;
};

namespace detail {
struct TokenTag {};
}

using Token = util::Tagged<std::string, detail::TokenTag>;

class PlayerTokens {
public:
    Token AddPlayer(Player& player) {
        Token token = GenerateToken();
        token_to_player_.emplace(token, &player);
        return token;
    }

    Player* FindPlayerByToken(const Token& token) noexcept {
        const auto it = token_to_player_.find(token);
        return it == token_to_player_.end() ? nullptr : it->second;
    }

    const Player* FindPlayerByToken(const Token& token) const noexcept {
        const auto it = token_to_player_.find(token);
        return it == token_to_player_.end() ? nullptr : it->second;
    }

    void RestoreToken(Token token, Player& player) {
        if (!token_to_player_.emplace(std::move(token), &player).second) {
            throw std::runtime_error("Duplicate player token in saved state");
        }
    }

    void RemovePlayer(const Player& player) {
        std::erase_if(token_to_player_, [&player](const auto& item) {
            return item.second == &player;
        });
    }

    const auto& GetTokenToPlayer() const noexcept {
        return token_to_player_;
    }

    void Clear() noexcept {
        token_to_player_.clear();
    }

private:
    Token GenerateToken() {
        std::ostringstream stream;

        stream << std::hex
               << std::setfill('0')
               << std::setw(16)
               << generator1_()
               << std::setw(16)
               << generator2_();

        return Token{stream.str()};
    }

    static std::mt19937_64::result_type GenerateSeed() {
        std::random_device random_device;
        std::uniform_int_distribution<std::mt19937_64::result_type> distribution;
        return distribution(random_device);
    }

private:
    std::mt19937_64 generator1_{GenerateSeed()};
    std::mt19937_64 generator2_{GenerateSeed()};

    std::unordered_map<
        Token,
        Player*,
        util::TaggedHasher<Token>
    > token_to_player_;
};

class Game {
public:
    using Maps = std::vector<Map>;

    void AddMap(Map map);
    void SetLootGeneratorConfig(std::chrono::milliseconds period, double probability) {
        loot_period_ = period; loot_probability_ = probability;
    }
    std::vector<RetiredPlayerData> UpdateState(std::uint64_t time_delta_ms);
    void SetDogRetirementTime(std::chrono::milliseconds value) noexcept { dog_retirement_time_ = value; }

    const Maps& GetMaps() const noexcept {
        return maps_;
    }

    const Map* FindMap(const Map::Id& id) const noexcept {
        const auto it = map_id_to_index_.find(id);

        if (it != map_id_to_index_.end()) {
            return &maps_.at(it->second);
        }

        return nullptr;
    }

    GameSession* FindGameSession(const Map::Id& map_id) noexcept {
        const auto it = map_id_to_session_.find(map_id);

        if (it != map_id_to_session_.end()) {
            return &sessions_.at(it->second);
        }

        return nullptr;
    }

    const GameSession* FindGameSession(const Map::Id& map_id) const noexcept {
        const auto it = map_id_to_session_.find(map_id);

        if (it != map_id_to_session_.end()) {
            return &sessions_.at(it->second);
        }

        return nullptr;
    }

    GameSession& GetOrCreateGameSession(const Map::Id& map_id);

    Players& GetPlayers() noexcept {
        return players_;
    }

    const Players& GetPlayers() const noexcept {
        return players_;
    }

    PlayerTokens& GetPlayerTokens() noexcept {
        return player_tokens_;
    }

    const PlayerTokens& GetPlayerTokens() const noexcept {
        return player_tokens_;
    }

    const std::deque<GameSession>& GetSessions() const noexcept {
        return sessions_;
    }

    void ClearRuntimeState() {
        sessions_.clear();
        map_id_to_session_.clear();
        players_.Clear();
        player_tokens_.Clear();
    }

private:
    using MapIdHasher = util::TaggedHasher<Map::Id>;

    using MapIdToIndex = std::unordered_map<
        Map::Id,
        std::size_t,
        MapIdHasher
    >;

    Maps maps_;
    MapIdToIndex map_id_to_index_;

    std::deque<GameSession> sessions_;
    MapIdToIndex map_id_to_session_;

    Players players_;
    PlayerTokens player_tokens_;
    std::chrono::milliseconds loot_period_{1000};
    double loot_probability_ = 0.0;
    std::chrono::milliseconds dog_retirement_time_{60000};
};

}  // namespace model
