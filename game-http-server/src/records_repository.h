#pragma once

#include <pqxx/pqxx>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace records {

struct RetiredPlayer {
    std::string name;
    std::uint32_t score = 0;
    double play_time = 0.0;
};

class Repository {
public:
    explicit Repository(std::string db_url);

    void Save(const RetiredPlayer& player);
    std::vector<RetiredPlayer> Load(std::size_t start, std::size_t max_items);

private:
    pqxx::connection connection_;
};

}  // namespace records
