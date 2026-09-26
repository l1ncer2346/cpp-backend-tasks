#pragma once

#include <boost/json.hpp>

#include <string>
#include <unordered_map>

namespace extra_data {

// Map data the model does not need, only the client
class MapsExtra {
public:
    void SetLootTypes(const std::string& map_id, boost::json::array loot_types) {
        loot_types_[map_id] = std::move(loot_types);
    }

    const boost::json::array* FindLootTypes(const std::string& map_id) const {
        if (auto it = loot_types_.find(map_id); it != loot_types_.end()) {
            return &it->second;
        }
        return nullptr;
    }

private:
    std::unordered_map<std::string, boost::json::array> loot_types_;
};

}  // namespace extra_data
