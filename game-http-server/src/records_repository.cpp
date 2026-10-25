#include "records_repository.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace records {

Repository::Repository(std::string db_url)
    : connection_{std::move(db_url)} {
    pqxx::work tx{connection_};
    tx.exec(R"SQL(
CREATE TABLE IF NOT EXISTS retired_players (
    id UUID PRIMARY KEY,
    name varchar(100) NOT NULL,
    score integer NOT NULL,
    play_time_ms bigint NOT NULL
);
)SQL");
    tx.exec(R"SQL(
CREATE INDEX IF NOT EXISTS retired_players_order_idx
ON retired_players (score DESC, play_time_ms ASC, name ASC);
)SQL");
    tx.commit();
}

void Repository::Save(const RetiredPlayer& player) {
    pqxx::work tx{connection_};
    const auto id = boost::uuids::to_string(boost::uuids::random_generator()());
    const auto play_time_ms = static_cast<std::int64_t>(player.play_time * 1000.0);
    tx.exec_params(
        "INSERT INTO retired_players (id, name, score, play_time_ms) VALUES ($1, $2, $3, $4)",
        id, player.name, player.score, play_time_ms);
    tx.commit();
}

std::vector<RetiredPlayer> Repository::Load(std::size_t start, std::size_t max_items) {
    pqxx::read_transaction tx{connection_};
    const auto rows = tx.exec_params(R"SQL(
SELECT name, score, play_time_ms
FROM retired_players
ORDER BY score DESC, play_time_ms ASC, name ASC
LIMIT $1 OFFSET $2
)SQL", max_items, start);

    std::vector<RetiredPlayer> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back({
            row["name"].as<std::string>(),
            row["score"].as<std::uint32_t>(),
            row["play_time_ms"].as<double>() / 1000.0
        });
    }
    return result;
}

}  // namespace records
