#include "state_storage.h"

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include <fstream>
#include <stdexcept>

#include "model_serialization.h"

namespace state_storage {

void Save(const std::filesystem::path& path, const model::GameSnapshot& snapshot) {
    auto temp_path = path;
    temp_path += ".tmp";
    {
        std::ofstream output(temp_path);
        if (!output) {
            throw std::runtime_error("Failed to open state file: " + temp_path.string());
        }
        boost::archive::text_oarchive archive{output};
        archive << snapshot;
    }
    std::filesystem::rename(temp_path, path);
}

std::optional<model::GameSnapshot> Load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open state file: " + path.string());
    }
    model::GameSnapshot snapshot;
    try {
        boost::archive::text_iarchive archive{input};
        archive >> snapshot;
    } catch (const std::exception& error) {
        throw std::runtime_error("Failed to restore state: " + std::string(error.what()));
    }
    return snapshot;
}

}  // namespace state_storage
