#include "project/ProjectSerializer.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#ifdef _WIN32
#include <windows.h>
#endif
namespace jeff::daw {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TimingMarker, sourceSeconds, destinationBeats)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TimingMap, markers)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AudioAsset, id, relativePath, checksum, channels, sourceRate,
                                   frameCount, extensions)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TimingGroup, id, mode, timingMap, importedTimingMap,
                                   syncReferenceTrackId, referenceUnavailable, extensions)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Track, id, name, assetId, timingGroupId, placementBeats,
                                   importedPlacementBeats, trimStartSeconds, trimEndSeconds,
                                   importedTrimStartSeconds, importedTrimEndSeconds, gain, pan,
                                   mute, solo, sourceBpm, downbeatSeconds, importedSourceBpm,
                                   importedDownbeatSeconds, extensions)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Project, schemaVersion, projectId, revisionId, parentRevisionId,
                                   tempoBpm, tracks, assets, groups, playheadBeats, extensions)
namespace {
constexpr std::size_t maxDocument = 8 * 1024 * 1024;
}
Result<std::string> serializeProject(const Project &p) {
  auto valid = validate(p);
  if (!valid)
    return Result<std::string>::failure(valid.error().code, valid.error().message);
  try {
    auto text = nlohmann::json(p).dump(2);
    if (text.size() > maxDocument)
      throw std::runtime_error("Project document exceeds 8 MiB.");
    return Result<std::string>::success(std::move(text));
  } catch (const std::exception &e) {
    return Result<std::string>::failure(ErrorCode::writeFailure, e.what());
  }
}
Result<Project> deserializeProject(const std::string &text) {
  try {
    if (text.size() > maxDocument)
      throw std::runtime_error("Project document exceeds 8 MiB.");
    auto json = nlohmann::json::parse(text);
    if (json.at("schemaVersion").get<int>() != currentProjectSchemaVersion)
      throw std::runtime_error("Unsupported project schema version.");
    for (auto name : {"tracks", "assets", "groups"})
      if (!json.at(name).is_array() || json.at(name).size() > 4096)
        throw std::runtime_error("Project entity limit exceeded.");
    auto p = json.get<Project>();
    auto valid = validate(p);
    if (!valid)
      return Result<Project>::failure(valid.error().code, valid.error().message);
    return Result<Project>::success(std::move(p));
  } catch (const std::exception &e) {
    return Result<Project>::failure(ErrorCode::invalidProject, e.what());
  }
}
Result<void> saveProject(const std::filesystem::path &path, const Project &p) {
  auto text = serializeProject(p);
  if (!text)
    return Result<void>::failure(text.error().code, text.error().message);
  auto temp = path;
  temp += std::string(".partial-") + std::to_string(std::random_device{}());
  try {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(text.value().data(), std::streamsize(text.value().size()));
    out.close();
    if (!out)
      throw std::runtime_error("Cannot finish project write.");
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("Cannot replace project document.");
#else
    std::filesystem::rename(temp, path);
#endif
    return Result<void>::success();
  } catch (const std::exception &e) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Result<void>::failure(ErrorCode::writeFailure, e.what());
  }
}
Result<Project> loadProject(const std::filesystem::path &path) {
  try {
    if (std::filesystem::file_size(path) > maxDocument)
      throw std::runtime_error("Project document exceeds 8 MiB.");
    std::ifstream in(path, std::ios::binary);
    if (!in)
      throw std::runtime_error("Cannot open project document.");
    std::string text((std::istreambuf_iterator<char>(in)), {});
    if (in.bad())
      throw std::runtime_error("Cannot read project document.");
    return deserializeProject(text);
  } catch (const std::exception &e) {
    return Result<Project>::failure(ErrorCode::readFailure, e.what());
  }
}
} // namespace jeff::daw
