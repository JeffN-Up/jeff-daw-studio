#pragma once

#include "project/Project.h"
#include <filesystem>
#include <string>
#include <string_view>

namespace jeff::daw {

Result<std::string> serializeProject(const Project&);
Result<Project> deserializeProject(std::string_view);
Result<void> saveProject(const Project&, const std::filesystem::path&);
Result<Project> loadProject(const std::filesystem::path&);

} // namespace jeff::daw
