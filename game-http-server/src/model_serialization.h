#pragma once

#include "model.h"

#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace model {

template <typename Archive>
void serialize(Archive& ar, Position& value, [[maybe_unused]] const unsigned version) {
    ar& value.x;
    ar& value.y;
}

template <typename Archive>
void serialize(Archive& ar, Speed& value, [[maybe_unused]] const unsigned version) {
    ar& value.x;
    ar& value.y;
}

template <typename Archive>
void serialize(Archive& ar, BagItem& value, [[maybe_unused]] const unsigned version) {
    ar& value.id;
    ar& value.type;
}

template <typename Archive>
void serialize(Archive& ar, LostObject& value, [[maybe_unused]] const unsigned version) {
    ar& value.id;
    ar& value.type;
    ar& value.position;
}

}  // namespace model

namespace serialization {

class DogRepr {
public:
    DogRepr() = default;

    explicit DogRepr(const model::Dog& dog)
        : id_{dog.GetId()}
        , name_{dog.GetName()}
        , position_{dog.GetPosition()}
        , speed_{dog.GetSpeed()}
        , direction_{dog.GetDirection()}
        , bag_{dog.GetBag()}
        , score_{dog.GetScore()} {
    }

    model::Dog Restore() const {
        model::Dog dog{id_, name_, position_};
        dog.SetSpeed(speed_);
        dog.SetDirection(direction_);
        for (const auto& item : bag_) {
            dog.AddToBag(item);
        }
        dog.AddScore(score_);
        return dog;
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& id_;
        ar& name_;
        ar& position_;
        ar& speed_;
        ar& direction_;
        ar& bag_;
        ar& score_;
    }

private:
    model::Dog::Id id_ = 0;
    std::string name_;
    model::Position position_;
    model::Speed speed_;
    model::Direction direction_ = model::Direction::NORTH;
    std::vector<model::BagItem> bag_;
    std::uint32_t score_ = 0;
};

struct SessionRepr {
    std::string map_id;
    std::vector<DogRepr> dogs;
    std::vector<model::LostObject> lost_objects;

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& map_id;
        ar& dogs;
        ar& lost_objects;
    }
};

struct PlayerRepr {
    model::Player::Id player_id = 0;
    std::string map_id;
    model::Dog::Id dog_id = 0;
    std::string token;

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& player_id;
        ar& map_id;
        ar& dog_id;
        ar& token;
    }
};

class GameStateRepr {
public:
    GameStateRepr() = default;
    explicit GameStateRepr(const model::Game& game);

    void Restore(model::Game& game) const;

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& sessions_;
        ar& players_;
    }

private:
    std::vector<SessionRepr> sessions_;
    std::vector<PlayerRepr> players_;
};

}  // namespace serialization
