#pragma once

#include <filesystem>
#include <optional>

#include "model.h"

namespace state_storage {

// Запись идёт во временный файл с последующим переименованием
void Save(const std::filesystem::path& path, const model::GameSnapshot& snapshot);

// Пустой результат, если файла нет
std::optional<model::GameSnapshot> Load(const std::filesystem::path& path);

}  // namespace state_storage
