#pragma once
#include "project/Project.h"
#include <filesystem>
namespace jeff::daw {
Result<std::string> serializeProject(const Project &);
Result<Project> deserializeProject(const std::string &);
Result<void> saveProject(const std::filesystem::path &, const Project &);
Result<Project> loadProject(const std::filesystem::path &);
} // namespace jeff::daw
