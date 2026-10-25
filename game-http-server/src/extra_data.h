#pragma once
#include "model.h"
#include <boost/json.hpp>
#include <unordered_map>

namespace extra_data {
class ExtraData {
public:
    void SetLootTypes(const model::Map::Id& id, boost::json::array value) { data_[id] = std::move(value); }
    const boost::json::array& GetLootTypes(const model::Map::Id& id) const { return data_.at(id); }
private:
    std::unordered_map<model::Map::Id, boost::json::array, util::TaggedHasher<model::Map::Id>> data_;
};
}
