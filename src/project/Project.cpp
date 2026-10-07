#include "project/Project.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <limits>
#include <stdexcept>

namespace jeff::daw {
namespace {
Result<void> invalid(std::string message) {
  return Result<void>::failure(ErrorCode::invalidProject, std::move(message));
}

template <typename T>
Result<void> validateIds(const std::vector<T>& entities) {
  std::unordered_set<Id> ids;
  for (const auto& entity : entities) {
    if (entity.id.empty()) return invalid("Entity ID must not be empty.");
    if (!ids.insert(entity.id).second)
      return Result<void>::failure(ErrorCode::duplicateId, "Entity IDs must be unique within their collection.");
  }
  return Result<void>::success();
}

template <typename T>
const T* find(const std::vector<T>& entities, const Id& id) {
  const auto it = std::find_if(entities.begin(), entities.end(), [&](const auto& entity) { return entity.id == id; });
  return it == entities.end() ? nullptr : &*it;
}

bool validRelativePath(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != std::string::npos ||
      path.find('\0') != std::string::npos) return false;
  std::size_t start = 0;
  while (start < path.size()) {
    const auto end = path.find_first_of("/\\", start);
    const auto part = path.substr(start, end == std::string::npos ? end : end - start);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string::npos) return true;
    start = end + 1;
  }
  return false;
}

bool validTrim(double start, double end, double duration) {
  return std::isfinite(start) && std::isfinite(end) && start >= 0.0 && end > start && end <= duration;
}
}

Result<void> validate(const Project& project) {
  if (project.schemaVersion != currentProjectSchemaVersion)
    return invalid("Unsupported project schema version.");
  if (project.projectId.empty() || project.revisionId.empty()) return invalid("Project and revision IDs are required.");
  if (!std::isfinite(project.tempoBpm) || project.tempoBpm <= 0.0 || !std::isfinite(project.playheadBeats))
    return invalid("Project tempo must be positive and tempo/playhead must be finite.");
  for (const auto result : {validateIds(project.assets), validateIds(project.groups), validateIds(project.tracks)})
    if (!result) return result;
  for (const auto& asset : project.assets) {
    if (!validRelativePath(asset.relativePath) || asset.checksum.empty())
      return invalid("Media needs a safe relative path and checksum.");
    if (asset.channels <= 0 || asset.sourceRate <= 0 || asset.frameCount <= 0)
      return invalid("Media channels, source rate and frame count must be positive.");
  }
  for (const auto& group : project.groups) {
    if (group.mode != TimingMode::preserve && group.mode != TimingMode::edit && group.mode != TimingMode::autoSync)
      return invalid("Unknown timing mode.");
    if (const auto result = validate(group.timingMap); !result) return result;
    if (const auto result = validate(group.importedTimingMap); !result) return result;
    if (!group.syncReferenceTrackId.empty() && !group.referenceUnavailable &&
        !find(project.tracks, group.syncReferenceTrackId))
      return invalid("Sync reference must exist or be marked unavailable.");
  }
  for (const auto& track : project.tracks) {
    const auto* asset = find(project.assets, track.assetId);
    if (!asset || !find(project.groups, track.timingGroupId))
      return Result<void>::failure(ErrorCode::missingEntity, "Track media and timing group must exist.");
    const double duration = static_cast<double>(asset->frameCount) / asset->sourceRate;
    if (!validTrim(track.trimStartSeconds, track.trimEndSeconds, duration) ||
        !validTrim(track.importedTrimStartSeconds, track.importedTrimEndSeconds, duration))
      return invalid("Clip bounds must have positive duration within source media.");
    if (!std::isfinite(track.placementBeats) || !std::isfinite(track.importedPlacementBeats) ||
        !std::isfinite(track.gain) || track.gain < 0.0 || !std::isfinite(track.pan) || track.pan < -1.0 || track.pan > 1.0)
      return invalid("Placement and mixer values must be finite, gain nonnegative and pan within [-1, 1].");
    if (!std::isfinite(track.sourceBpm) || track.sourceBpm < 0.0 ||
        !std::isfinite(track.importedSourceBpm) || track.importedSourceBpm < 0.0 ||
        !std::isfinite(track.downbeatSeconds) || track.downbeatSeconds < 0.0 || track.downbeatSeconds > duration ||
        !std::isfinite(track.importedDownbeatSeconds) || track.importedDownbeatSeconds < 0.0 || track.importedDownbeatSeconds > duration)
      return invalid("Source tempo and downbeat must be valid source timing values.");
  }
  return Result<void>::success();
}
Result<double> groupOriginBeats(const Project& project, const Id& groupId) {
  double origin = std::numeric_limits<double>::infinity();
  for (const auto& track : project.tracks)
    if (track.timingGroupId == groupId) origin = std::min(origin, track.placementBeats);
  if (!std::isfinite(origin)) return Result<double>::failure(ErrorCode::missingEntity, "Timing group has no finite member placement.");
  return Result<double>::success(origin);
}

Result<double> sourceGridOffset(const Project& project, const Track& track) {
  using R = Result<double>;
  const auto* group = find(project.groups, track.timingGroupId);
  if (!group) return R::failure(ErrorCode::missingEntity, "Track timing group does not exist.");
  auto origin = groupOriginBeats(project, group->id);
  if (!origin) return origin;
  if (!std::isfinite(project.tempoBpm) || project.tempoBpm <= 0 || !std::isfinite(track.placementBeats))
    return R::failure(ErrorCode::invalidProject, "Source grid needs finite placement and positive tempo.");
  try {
    if (group->mode == TimingMode::preserve) {
      const double offset = (track.placementBeats - origin.value()) * 60 / project.tempoBpm;
      if (!std::isfinite(offset)) throw std::overflow_error("Source grid offset overflow.");
      return R::success(offset);
    }
    const auto valid = validate(group->timingMap);
    if (!valid) return R::failure(valid.error().code, valid.error().message);
    const double beat = mapTime(group->timingMap, 0) + track.placementBeats - origin.value();
    const auto& markers = group->timingMap.markers;
    const auto after = std::upper_bound(markers.begin(), markers.end(), beat,
        [](double v, const auto& marker) { return v < marker.destinationBeats; });
    const auto right = after == markers.begin() ? 1 : after == markers.end() ? markers.size()-1 : std::size_t(after-markers.begin());
    const auto& a = markers[right-1];
    const auto& b = markers[right];
    const double offset = std::lerp(a.sourceSeconds, b.sourceSeconds,
        (beat-a.destinationBeats)/(b.destinationBeats-a.destinationBeats));
    if (!std::isfinite(offset) || !std::isfinite(beat)) throw std::overflow_error("Source grid offset overflow.");
    return R::success(offset);
  } catch (const std::exception& e) {
    return R::failure(ErrorCode::invalidTimingMap, e.what());
  }
}

Result<double> sourceTimeToProjectBeat(const Project& project, const Track& track, double seconds) {
  using R = Result<double>;
  auto offset = sourceGridOffset(project, track);
  if (!offset) return offset;
  auto origin = groupOriginBeats(project, track.timingGroupId);
  if (!origin) return origin;
  const auto* group = find(project.groups, track.timingGroupId);
  try {
    const double beat = origin.value() + (group->mode == TimingMode::preserve
        ? (offset.value()+seconds)*project.tempoBpm/60
        : mapTime(group->timingMap, offset.value()+seconds));
    if (!std::isfinite(seconds) || !std::isfinite(beat)) throw std::overflow_error("Source grid position overflow.");
    return R::success(beat);
  } catch (const std::exception& e) {
    return R::failure(ErrorCode::invalidTimingMap, e.what());
  }
}
} // namespace jeff::daw
