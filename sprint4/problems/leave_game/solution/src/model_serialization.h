#pragma once

#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

#include "model.h"

namespace model {

template <typename Archive>
void serialize(Archive& ar, Point& point, [[maybe_unused]] const unsigned version) {
    ar& point.x;
    ar& point.y;
}

template <typename Archive>
void serialize(Archive& ar, LootItem& item, [[maybe_unused]] const unsigned version) {
    ar& item.id;
    ar& item.type;
}

template <typename Archive>
void serialize(Archive& ar, LostObject& object, [[maybe_unused]] const unsigned version) {
    ar& object.item;
    ar& object.position;
}

template <typename Archive>
void serialize(Archive& ar, PlayerSnapshot& player, [[maybe_unused]] const unsigned version) {
    ar& player.id;
    ar& player.name;
    ar& player.token;
    ar& player.map_id;
    ar& player.position;
    ar& player.speed;
    ar& player.direction;
    ar& player.bag;
    ar& player.score;
    ar& player.idle_time_ms;
    ar& player.play_time_ms;
}

template <typename Archive>
void serialize(Archive& ar, MapLootSnapshot& loot, [[maybe_unused]] const unsigned version) {
    ar& loot.map_id;
    ar& loot.objects;
}

template <typename Archive>
void serialize(Archive& ar, GameSnapshot& snapshot, [[maybe_unused]] const unsigned version) {
    ar& snapshot.players;
    ar& snapshot.loot;
    ar& snapshot.next_player_id;
    ar& snapshot.next_loot_id;
}

}  // namespace model
