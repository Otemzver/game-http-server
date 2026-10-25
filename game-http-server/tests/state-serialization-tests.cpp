#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "../src/model.h"
#include "../src/model_serialization.h"

using namespace model;
using namespace std::literals;

TEST_CASE("Position serialization") {
    std::stringstream strm;
    const Position pos{10.5, 20.25};
    {
        boost::archive::text_oarchive out{strm};
        out << pos;
    }
    Position restored;
    {
        boost::archive::text_iarchive in{strm};
        in >> restored;
    }
    CHECK(pos.x == restored.x);
    CHECK(pos.y == restored.y);
}

TEST_CASE("Dog serialization") {
    Dog dog{42, "Pluto"s, {42.2, 12.5}};
    dog.AddScore(42);
    dog.AddToBag({10, 2});
    dog.SetDirection(Direction::EAST);
    dog.SetSpeed({2.3, -1.2});

    std::stringstream strm;
    {
        boost::archive::text_oarchive out{strm};
        out << serialization::DogRepr{dog};
    }

    serialization::DogRepr repr;
    {
        boost::archive::text_iarchive in{strm};
        in >> repr;
    }
    const auto restored = repr.Restore();

    CHECK(dog.GetId() == restored.GetId());
    CHECK(dog.GetName() == restored.GetName());
    CHECK(dog.GetPosition().x == restored.GetPosition().x);
    CHECK(dog.GetPosition().y == restored.GetPosition().y);
    CHECK(dog.GetSpeed().x == restored.GetSpeed().x);
    CHECK(dog.GetSpeed().y == restored.GetSpeed().y);
    CHECK(dog.GetDirection() == restored.GetDirection());
    CHECK(dog.GetScore() == restored.GetScore());
    REQUIRE(dog.GetBag().size() == restored.GetBag().size());
    CHECK(dog.GetBag().front().id == restored.GetBag().front().id);
    CHECK(dog.GetBag().front().type == restored.GetBag().front().type);
}
