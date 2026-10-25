#include "model_serialization.h"

#include <unordered_map>

namespace serialization {

GameStateRepr::GameStateRepr(const model::Game& game) {
    sessions_.reserve(game.GetSessions().size());
    for (const auto& session : game.GetSessions()) {
        SessionRepr session_repr;
        session_repr.map_id = *session.GetMap().GetId();
        session_repr.dogs.reserve(session.GetDogs().size());
        for (const auto& dog : session.GetDogs()) {
            session_repr.dogs.emplace_back(dog);
        }
        session_repr.lost_objects.assign(
            session.GetLostObjects().begin(), session.GetLostObjects().end());
        sessions_.push_back(std::move(session_repr));
    }

    std::unordered_map<const model::Player*, std::string> tokens;
    for (const auto& [token, player] : game.GetPlayerTokens().GetTokenToPlayer()) {
        tokens.emplace(player, *token);
    }

    players_.reserve(game.GetPlayers().GetPlayers().size());
    for (const auto& player : game.GetPlayers().GetPlayers()) {
        const auto token_it = tokens.find(&player);
        if (token_it == tokens.end()) {
            throw std::runtime_error("Player has no authentication token");
        }
        players_.push_back({
            player.GetId(),
            *player.GetSession().GetMap().GetId(),
            player.GetDog().GetId(),
            token_it->second
        });
    }
}

void GameStateRepr::Restore(model::Game& game) const {
    game.ClearRuntimeState();

    for (const auto& session_repr : sessions_) {
        auto& session = game.GetOrCreateGameSession(model::Map::Id{session_repr.map_id});
        for (const auto& dog_repr : session_repr.dogs) {
            session.RestoreDog(dog_repr.Restore());
        }
        for (const auto& object : session_repr.lost_objects) {
            session.RestoreLostObject(object);
        }
    }

    for (const auto& player_repr : players_) {
        auto* session = game.FindGameSession(model::Map::Id{player_repr.map_id});
        if (!session) {
            throw std::runtime_error("Saved player refers to an unknown session");
        }
        auto* dog = session->FindDogById(player_repr.dog_id);
        if (!dog) {
            throw std::runtime_error("Saved player refers to an unknown dog");
        }
        auto& player = game.GetPlayers().Restore(player_repr.player_id, *dog, *session);
        game.GetPlayerTokens().RestoreToken(model::Token{player_repr.token}, player);
    }
}

}  // namespace serialization
