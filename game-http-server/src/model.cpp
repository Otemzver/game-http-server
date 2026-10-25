#include "model.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace model {

using namespace std::literals;

namespace {

constexpr double ROAD_HALF_WIDTH = 0.4;
constexpr double EPSILON = 1e-9;
constexpr int POSITION_SEARCH_ITERATIONS = 80;
constexpr double MILLISECONDS_PER_SECOND = 1000.0;

bool IsInsideRoad(const Position& position, const Road& road) {
    const Point start = road.GetStart();
    const Point end = road.GetEnd();

    if (road.IsHorizontal()) {
        const double min_x = static_cast<double>(std::min(start.x, end.x)) - ROAD_HALF_WIDTH;
        const double max_x = static_cast<double>(std::max(start.x, end.x)) + ROAD_HALF_WIDTH;
        const double min_y = static_cast<double>(start.y) - ROAD_HALF_WIDTH;
        const double max_y = static_cast<double>(start.y) + ROAD_HALF_WIDTH;

        return position.x >= min_x - EPSILON
            && position.x <= max_x + EPSILON
            && position.y >= min_y - EPSILON
            && position.y <= max_y + EPSILON;
    }

    const double min_x = static_cast<double>(start.x) - ROAD_HALF_WIDTH;
    const double max_x = static_cast<double>(start.x) + ROAD_HALF_WIDTH;
    const double min_y = static_cast<double>(std::min(start.y, end.y)) - ROAD_HALF_WIDTH;
    const double max_y = static_cast<double>(std::max(start.y, end.y)) + ROAD_HALF_WIDTH;

    return position.x >= min_x - EPSILON
        && position.x <= max_x + EPSILON
        && position.y >= min_y - EPSILON
        && position.y <= max_y + EPSILON;
}

bool IsInsideAnyRoad(const Position& position, const Map& map) {
    return std::any_of(
        map.GetRoads().begin(),
        map.GetRoads().end(),
        [&position](const Road& road) {
            return IsInsideRoad(position, road);
        }
    );
}

Position FindFarthestAllowedPosition(
    const Position& start,
    const Position& target,
    const Map& map
) {
    if (IsInsideAnyRoad(target, map)) {
        return target;
    }

    double low = 0.0;
    double high = 1.0;

    for (int iteration = 0; iteration < POSITION_SEARCH_ITERATIONS; ++iteration) {
        const double middle = (low + high) / 2.0;

        const Position candidate{
            start.x + (target.x - start.x) * middle,
            start.y + (target.y - start.y) * middle
        };

        if (IsInsideAnyRoad(candidate, map)) {
            low = middle;
        } else {
            high = middle;
        }
    }

    return {
        start.x + (target.x - start.x) * low,
        start.y + (target.y - start.y) * low
    };
}

}  // namespace

void Map::AddOffice(Office office) {
    const std::size_t index = offices_.size();

    if (warehouse_id_to_index_.contains(office.GetId())) {
        throw std::invalid_argument("Duplicate warehouse");
    }

    offices_.emplace_back(std::move(office));
    warehouse_id_to_index_.emplace(offices_.back().GetId(), index);
}

Dog& GameSession::RestoreDog(Dog dog) {
    next_dog_id_ = std::max(next_dog_id_, static_cast<Dog::Id>(dog.GetId() + 1));
    dogs_.push_back(std::move(dog));
    return dogs_.back();
}

void GameSession::RestoreLostObject(LostObject object) {
    next_lost_object_id_ = std::max(
        next_lost_object_id_,
        static_cast<LostObject::Id>(object.id + 1)
    );
    lost_objects_.push_back(std::move(object));
}

Dog* GameSession::FindDogById(Dog::Id id) noexcept {
    const auto it = std::find_if(dogs_.begin(), dogs_.end(), [id](const Dog& dog) {
        return dog.GetId() == id;
    });
    return it == dogs_.end() ? nullptr : &*it;
}

Dog& GameSession::AddDog(std::string name) {
    dogs_.emplace_back(
        next_dog_id_++,
        std::move(name),
        GetInitialPosition()
    );

    return dogs_.back();
}

Position GameSession::GetInitialPosition() const {
    const auto& roads = map_->GetRoads();

    if (roads.empty()) {
        return {0.0, 0.0};
    }

    const Point start = roads.front().GetStart();

    return {
        static_cast<double>(start.x),
        static_cast<double>(start.y)
    };
}

void GameSession::MoveDog(Dog& dog, double delta_seconds) const {
    const Position start = dog.GetPosition();
    const Speed speed = dog.GetSpeed();

    if (std::abs(speed.x) < EPSILON && std::abs(speed.y) < EPSILON) {
        return;
    }

    const Position target{
        start.x + speed.x * delta_seconds,
        start.y + speed.y * delta_seconds
    };

    if (IsInsideAnyRoad(target, *map_)) {
        dog.SetPosition(target);
        return;
    }

    dog.SetPosition(FindFarthestAllowedPosition(start, target, *map_));
    dog.SetSpeed({0.0, 0.0});
}

Position GameSession::GenerateRandomRoadPosition() {
    const auto& roads = map_->GetRoads();
    if (roads.empty()) return {0.0, 0.0};
    std::uniform_int_distribution<std::size_t> road_dist(0, roads.size() - 1);
    const Road& road = roads[road_dist(random_generator_)];
    const Point a = road.GetStart();
    const Point b = road.GetEnd();
    std::uniform_real_distribution<double> t_dist(0.0, 1.0);
    const double t = t_dist(random_generator_);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

void GameSession::GenerateLoot(std::chrono::milliseconds time_delta) {
    const unsigned count = loot_generator_.Generate(time_delta,
        static_cast<unsigned>(lost_objects_.size()),
        static_cast<unsigned>(dogs_.size()));
    if (map_->GetLootTypesCount() == 0) return;
    std::uniform_int_distribution<std::size_t> type_dist(0, map_->GetLootTypesCount() - 1);
    for (unsigned i = 0; i < count; ++i) {
        lost_objects_.push_back({next_lost_object_id_++, type_dist(random_generator_), GenerateRandomRoadPosition()});
    }
}

void GameSession::UpdateState(std::chrono::milliseconds time_delta) {
    const double delta_seconds = static_cast<double>(time_delta.count()) / MILLISECONDS_PER_SECOND;

    std::vector<Position> start_positions;
    start_positions.reserve(dogs_.size());
    for (const Dog& dog : dogs_) {
        start_positions.push_back(dog.GetPosition());
    }
    for (Dog& dog : dogs_) {
        MoveDog(dog, delta_seconds);
    }

    class Provider final : public collision_detector::ItemGathererProvider {
    public:
        Provider(const LostObjects& lost_objects, const Map::Offices& offices,
                 const Dogs& dogs, const std::vector<Position>& starts)
            : lost_objects_{lost_objects}, offices_{offices}, dogs_{dogs}, starts_{starts} {
        }

        size_t ItemsCount() const override {
            return lost_objects_.size() + offices_.size();
        }

        collision_detector::Item GetItem(size_t idx) const override {
            if (idx < lost_objects_.size()) {
                const auto& object = lost_objects_.at(idx);
                return {{object.position.x, object.position.y}, 0.0};
            }
            const auto office_pos = offices_.at(idx - lost_objects_.size()).GetPosition();
            return {{static_cast<double>(office_pos.x), static_cast<double>(office_pos.y)}, 0.25};
        }

        size_t GatherersCount() const override {
            return dogs_.size();
        }

        collision_detector::Gatherer GetGatherer(size_t idx) const override {
            const Position end = dogs_.at(idx).GetPosition();
            const Position start = starts_.at(idx);
            return {{start.x, start.y}, {end.x, end.y}, 0.3};
        }

    private:
        const LostObjects& lost_objects_;
        const Map::Offices& offices_;
        const Dogs& dogs_;
        const std::vector<Position>& starts_;
    };

    const std::size_t loot_count = lost_objects_.size();
    Provider provider{lost_objects_, map_->GetOffices(), dogs_, start_positions};
    const auto events = collision_detector::FindGatherEvents(provider);
    std::vector<bool> collected(loot_count, false);

    for (const auto& event : events) {
        Dog& dog = dogs_.at(event.gatherer_id);
        if (event.item_id < loot_count) {
            if (!collected[event.item_id] && !dog.IsBagFull(map_->GetBagCapacity())) {
                const auto& object = lost_objects_.at(event.item_id);
                dog.AddToBag({object.id, object.type});
                collected[event.item_id] = true;
            }
        } else {
            for (const BagItem& item : dog.GetBag()) {
                dog.AddScore(map_->GetLootValue(item.type));
            }
            dog.ClearBag();
        }
    }

    if (std::any_of(collected.begin(), collected.end(), [](bool value) { return value; })) {
        LostObjects remaining;
        for (std::size_t i = 0; i < lost_objects_.size(); ++i) {
            if (!collected[i]) {
                remaining.push_back(std::move(lost_objects_[i]));
            }
        }
        lost_objects_ = std::move(remaining);
    }

    GenerateLoot(time_delta);
}

void Game::AddMap(Map map) {
    const std::size_t index = maps_.size();

    if (map_id_to_index_.contains(map.GetId())) {
        throw std::invalid_argument(
            "Map with id "s + *map.GetId() + " already exists"s
        );
    }

    map_id_to_index_.emplace(map.GetId(), index);
    maps_.emplace_back(std::move(map));
}

std::vector<RetiredPlayerData> Game::UpdateState(std::uint64_t time_delta_ms) {
    const auto delta = std::chrono::milliseconds{time_delta_ms};
    for (GameSession& session : sessions_) {
        session.UpdateState(delta);
    }

    std::vector<RetiredPlayerData> retired;
    for (Player& player : players_.GetPlayers()) {
        if (player.IsRetired()) {
            continue;
        }
        player.AddPlayTime(time_delta_ms);
        player.AddIdleTime(time_delta_ms);

        if (player.GetIdleTimeMs() >= static_cast<std::uint64_t>(dog_retirement_time_.count())) {
            retired.push_back({player.GetDog().GetName(), player.GetDog().GetScore(), player.GetPlayTimeMs()});
            player.Retire();
            player_tokens_.RemovePlayer(player);
        }
    }
    return retired;
}

GameSession& Game::GetOrCreateGameSession(const Map::Id& map_id) {
    if (GameSession* session = FindGameSession(map_id)) {
        return *session;
    }

    const Map* map = FindMap(map_id);

    if (map == nullptr) {
        throw std::invalid_argument("Map not found"s);
    }

    const std::size_t index = sessions_.size();

    sessions_.emplace_back(*map, loot_period_, loot_probability_);
    map_id_to_session_.emplace(map_id, index);

    return sessions_.back();
}

}  // namespace model
