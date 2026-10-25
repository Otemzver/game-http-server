#include <catch2/catch_test_macros.hpp>
#include "../src/model.h"
using namespace std::chrono_literals;
TEST_CASE("Lost objects are generated on roads and their count does not exceed dogs") {
    model::Map map{model::Map::Id{"m"}, "map", 1.0, 2};
    map.AddRoad(model::Road{model::Road::HORIZONTAL, {0,0}, 10});
    model::GameSession session{map, 1ms, 1.0};
    session.AddDog("dog1"); session.AddDog("dog2");
    session.UpdateState(1ms);
    REQUIRE(session.GetLostObjects().size() == 2);
    for (const auto& loot : session.GetLostObjects()) {
        CHECK(loot.type < 2);
        CHECK(loot.position.x >= 0.0);
        CHECK(loot.position.x <= 10.0);
        CHECK(loot.position.y == 0.0);
    }
}
