#include "project/ProjectSerializer.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <set>

namespace jeff::daw {
namespace {
using Json = nlohmann::json;
constexpr std::uintmax_t maxProjectBytes = 16ULL * 1024 * 1024;

Result<Project> invalidProject(std::string message) {
  return Result<Project>::failure(ErrorCode::invalidProject, std::move(message));
}

void restoreExtensions(Json& object, const ExtensionFields& extensions) {
  for(const auto& [name, value] : extensions) {
    if(object.contains(name)) continue;
    object[name] = Json::parse(value);
  }
}

ExtensionFields captureExtensions(const Json& object,
                                  std::initializer_list<std::string_view> known) {
  std::set<std::string, std::less<>> names;
  for(const auto name : known) names.emplace(name);
  ExtensionFields extensions;
  for(auto item = object.begin(); item != object.end(); ++item)
    if(!names.contains(item.key())) extensions[item.key()] = item.value().dump();
  return extensions;
}

Json timingMapToJson(const TimingMap& map) {
  Json markers = Json::array();
  for(const auto& marker : map.markers)
    markers.push_back({{"sourceSeconds", marker.sourceSeconds},
                       {"destinationBeats", marker.destinationBeats}});
  return markers;
}

TimingMap timingMapFromJson(const Json& value) {
  if(!value.is_array() || value.size() > 100000)
    throw std::runtime_error("Timing markers must be a bounded array.");
  TimingMap map;
  for(const auto& marker : value) {
    if(!marker.is_object()) throw std::runtime_error("Timing marker must be an object.");
    map.markers.push_back({marker.at("sourceSeconds").get<double>(),
                           marker.at("destinationBeats").get<double>()});
  }
  return map;
}

std::string timingModeName(TimingMode mode) {
  switch(mode) {
    case TimingMode::preserve: return "preserve";
    case TimingMode::edit: return "edit";
    case TimingMode::autoSync: return "autoSync";
  }
  throw std::runtime_error("Unknown timing mode.");
}

TimingMode timingModeFromName(const std::string& name) {
  if(name == "preserve") return TimingMode::preserve;
  if(name == "edit") return TimingMode::edit;
  if(name == "autoSync") return TimingMode::autoSync;
  throw std::runtime_error("Unknown timing mode.");
}

Json toJson(const Project& project) {
  Json root{{"schemaVersion", project.schemaVersion}, {"projectId", project.projectId},
            {"revisionId", project.revisionId}, {"parentRevisionId", project.parentRevisionId},
            {"tempoBpm", project.tempoBpm}, {"playheadBeats", project.playheadBeats}};
  root["assets"] = Json::array();
  for(const auto& asset : project.assets) {
    Json value{{"id", asset.id}, {"relativePath", asset.relativePath},
               {"checksum", asset.checksum}, {"channels", asset.channels},
               {"sourceRate", asset.sourceRate}, {"frameCount", asset.frameCount}};
    restoreExtensions(value, asset.extensions);
    root["assets"].push_back(std::move(value));
  }
  root["groups"] = Json::array();
  for(const auto& group : project.groups) {
    Json value{{"id", group.id}, {"mode", timingModeName(group.mode)},
               {"timingMap", timingMapToJson(group.timingMap)},
               {"importedTimingMap", timingMapToJson(group.importedTimingMap)},
               {"syncReferenceTrackId", group.syncReferenceTrackId},
               {"referenceUnavailable", group.referenceUnavailable}};
    restoreExtensions(value, group.extensions);
    root["groups"].push_back(std::move(value));
  }
  root["tracks"] = Json::array();
  for(const auto& track : project.tracks) {
    Json value{{"id", track.id}, {"name", track.name}, {"assetId", track.assetId},
               {"timingGroupId", track.timingGroupId}, {"placementBeats", track.placementBeats},
               {"importedPlacementBeats", track.importedPlacementBeats},
               {"trimStartSeconds", track.trimStartSeconds}, {"trimEndSeconds", track.trimEndSeconds},
               {"importedTrimStartSeconds", track.importedTrimStartSeconds},
               {"importedTrimEndSeconds", track.importedTrimEndSeconds}, {"gain", track.gain},
               {"pan", track.pan}, {"mute", track.mute}, {"solo", track.solo},
               {"sourceBpm", track.sourceBpm}, {"downbeatSeconds", track.downbeatSeconds},
               {"importedSourceBpm", track.importedSourceBpm},
               {"importedDownbeatSeconds", track.importedDownbeatSeconds}};
    restoreExtensions(value, track.extensions);
    root["tracks"].push_back(std::move(value));
  }
  restoreExtensions(root, project.extensions);
  return root;
}

Project fromJson(const Json& root) {
  if(!root.is_object()) throw std::runtime_error("Project document must be an object.");
  Project project;
  project.schemaVersion = root.at("schemaVersion").get<int>();
  if(project.schemaVersion != currentProjectSchemaVersion)
    throw std::runtime_error("Unsupported project schema version.");
  project.projectId = root.at("projectId").get<std::string>();
  project.revisionId = root.at("revisionId").get<std::string>();
  project.parentRevisionId = root.value("parentRevisionId", std::string{});
  project.tempoBpm = root.at("tempoBpm").get<double>();
  project.playheadBeats = root.value("playheadBeats", 0.0);

  const auto& assets = root.at("assets");
  const auto& groups = root.at("groups");
  const auto& tracks = root.at("tracks");
  if(!assets.is_array() || !groups.is_array() || !tracks.is_array() ||
     assets.size() > 10000 || groups.size() > 10000 || tracks.size() > 10000)
    throw std::runtime_error("Project collections must be bounded arrays.");
  for(const auto& value : assets) {
    AudioAsset asset;
    asset.id = value.at("id").get<std::string>();
    asset.relativePath = value.at("relativePath").get<std::string>();
    asset.checksum = value.at("checksum").get<std::string>();
    asset.channels = value.at("channels").get<int>();
    asset.sourceRate = value.at("sourceRate").get<int>();
    asset.frameCount = value.at("frameCount").get<Frame>();
    asset.extensions = captureExtensions(value, {"id", "relativePath", "checksum", "channels",
                                                  "sourceRate", "frameCount"});
    project.assets.push_back(std::move(asset));
  }
  for(const auto& value : groups) {
    TimingGroup group;
    group.id = value.at("id").get<std::string>();
    group.mode = timingModeFromName(value.at("mode").get<std::string>());
    group.timingMap = timingMapFromJson(value.at("timingMap"));
    group.importedTimingMap = timingMapFromJson(value.at("importedTimingMap"));
    group.syncReferenceTrackId = value.value("syncReferenceTrackId", std::string{});
    group.referenceUnavailable = value.value("referenceUnavailable", false);
    group.extensions = captureExtensions(value, {"id", "mode", "timingMap", "importedTimingMap",
                                                  "syncReferenceTrackId", "referenceUnavailable"});
    project.groups.push_back(std::move(group));
  }
  for(const auto& value : tracks) {
    Track track;
    track.id = value.at("id").get<std::string>();
    track.name = value.at("name").get<std::string>();
    track.assetId = value.at("assetId").get<std::string>();
    track.timingGroupId = value.at("timingGroupId").get<std::string>();
    track.placementBeats = value.at("placementBeats").get<double>();
    track.importedPlacementBeats = value.at("importedPlacementBeats").get<double>();
    track.trimStartSeconds = value.at("trimStartSeconds").get<double>();
    track.trimEndSeconds = value.at("trimEndSeconds").get<double>();
    track.importedTrimStartSeconds = value.at("importedTrimStartSeconds").get<double>();
    track.importedTrimEndSeconds = value.at("importedTrimEndSeconds").get<double>();
    track.gain = value.at("gain").get<double>();
    track.pan = value.at("pan").get<double>();
    track.mute = value.at("mute").get<bool>();
    track.solo = value.at("solo").get<bool>();
    track.sourceBpm = value.value("sourceBpm", 0.0);
    track.downbeatSeconds = value.value("downbeatSeconds", 0.0);
    track.importedSourceBpm = value.value("importedSourceBpm", 0.0);
    track.importedDownbeatSeconds = value.value("importedDownbeatSeconds", 0.0);
    track.extensions = captureExtensions(value,
      {"id", "name", "assetId", "timingGroupId", "placementBeats", "importedPlacementBeats",
       "trimStartSeconds", "trimEndSeconds", "importedTrimStartSeconds", "importedTrimEndSeconds",
       "gain", "pan", "mute", "solo", "sourceBpm", "downbeatSeconds", "importedSourceBpm",
       "importedDownbeatSeconds"});
    project.tracks.push_back(std::move(track));
  }
  project.extensions = captureExtensions(root, {"schemaVersion", "projectId", "revisionId",
                                                 "parentRevisionId", "tempoBpm", "playheadBeats",
                                                 "assets", "groups", "tracks"});
  return project;
}

std::filesystem::path temporarySibling(const std::filesystem::path& path) {
  const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return path.parent_path() / (path.filename().string() + ".tmp-" + nonce);
}
} // namespace

Result<std::string> serializeProject(const Project& project) {
  if(const auto valid = validate(project); !valid)
    return Result<std::string>::failure(valid.error().code, valid.error().message);
  try {
    return Result<std::string>::success(toJson(project).dump(2));
  } catch(const std::exception& error) {
    return Result<std::string>::failure(ErrorCode::invalidProject,
                                        "Cannot serialize project: " + std::string(error.what()));
  }
}

Result<Project> deserializeProject(std::string_view text) {
  if(text.size() > maxProjectBytes) return invalidProject("Project document exceeds the size limit.");
  try {
    auto project = fromJson(Json::parse(text));
    if(const auto valid = validate(project); !valid)
      return Result<Project>::failure(valid.error().code, valid.error().message);
    return Result<Project>::success(std::move(project));
  } catch(const std::exception& error) {
    return invalidProject("Cannot read project: " + std::string(error.what()));
  }
}

Result<void> saveProject(const Project& project, const std::filesystem::path& path) {
  auto serialized = serializeProject(project);
  if(!serialized) return Result<void>::failure(serialized.error().code, serialized.error().message);
  if(path.empty()) return Result<void>::failure(ErrorCode::writeFailure, "Project path is empty.");
  const auto temporary = temporarySibling(path);
  std::ofstream output;
  auto fail = [&](std::string message) {
    if(output.is_open()) output.close();
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return Result<void>::failure(ErrorCode::writeFailure, std::move(message));
  };
  try {
    std::error_code error;
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if(error) return fail("Cannot create project folder: " + error.message());
    output.open(temporary, std::ios::binary | std::ios::trunc);
    if(!output) return fail("Cannot open temporary project file.");
    output.write(serialized.value().data(), std::streamsize(serialized.value().size()));
    output.flush();
    output.close();
    if(!output) return fail("Cannot finish temporary project file.");
    const auto backup = path.parent_path() / (path.filename().string() + ".recovery");
    const bool existed = std::filesystem::exists(path, error);
    if(error) return fail("Cannot inspect existing project file: " + error.message());
    if(existed) {
      std::filesystem::remove(backup, error);
      if(error) return fail("Cannot rotate the previous recovery file: " + error.message());
      error.clear();
      std::filesystem::rename(path, backup, error);
      if(error) return fail("Cannot preserve previous project file: " + error.message());
    }
    std::filesystem::rename(temporary, path, error);
    if(error) {
      if(existed) {
        std::error_code restore;
        std::filesystem::rename(backup, path, restore);
      }
      return fail("Cannot publish project file: " + error.message());
    }
    return Result<void>::success();
  } catch(const std::exception& error) {
    return fail("Cannot save project: " + std::string(error.what()));
  }
}

Result<Project> loadProject(const std::filesystem::path& path) {
  try {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if(error) return invalidProject("Cannot inspect project file: " + error.message());
    if(bytes > maxProjectBytes) return invalidProject("Project document exceeds the size limit.");
    std::ifstream input(path, std::ios::binary);
    if(!input) return invalidProject("Cannot open project file.");
    std::string text(std::size_t(bytes), '\0');
    input.read(text.data(), std::streamsize(text.size()));
    if(input.gcount() != std::streamsize(text.size()) || input.bad())
      return invalidProject("Project document is incomplete.");
    return deserializeProject(text);
  } catch(const std::exception& error) {
    return invalidProject("Cannot load project: " + std::string(error.what()));
  }
}

} // namespace jeff::daw
