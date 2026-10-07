#include "project/ProjectCommands.h"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace jeff::daw {
namespace {
Id newRevisionId() {
  std::random_device random;
  constexpr char digits[] = "0123456789abcdef";
  Id id;
  id.reserve(32);
  for (int i = 0; i < 4; ++i) {
    const auto value = random();
    for (int shift = 28; shift >= 0; shift -= 4) id += digits[(value >> shift) & 15];
  }
  return id;
}

template <typename T>
T* find(std::vector<T>& entities, const Id& id) {
  const auto it = std::find_if(entities.begin(), entities.end(), [&](const auto& entity) { return entity.id == id; });
  return it == entities.end() ? nullptr : &*it;
}
Result<void> missing() { return Result<void>::failure(ErrorCode::missingEntity, "Command target does not exist."); }
Result<void> invalid(std::string message) { return Result<void>::failure(ErrorCode::invalidCommand, std::move(message)); }

Result<void> edit(Project& project, const ProjectCommand& command) {
  return std::visit([&](const auto& value) -> Result<void> {
    using Command = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<Command, MoveGroup> || std::is_same_v<Command, SetGroupTimingMap> ||
                  std::is_same_v<Command, ResetTiming>) {
      auto* group = find(project.groups, value.groupId);
      if (!group) return missing();
      if constexpr (std::is_same_v<Command, MoveGroup>) {
        for (auto& track : project.tracks)
          if (track.timingGroupId == group->id) track.placementBeats += value.deltaBeats;
      } else if constexpr (std::is_same_v<Command, SetGroupTimingMap>) {
        group->timingMap = value.map;
        group->mode = value.mode;
      } else {
        group->timingMap = group->importedTimingMap;
        group->mode = TimingMode::preserve;
        group->syncReferenceTrackId.clear();
        group->referenceUnavailable = false;
        for (auto& track : project.tracks) {
          if (track.timingGroupId != group->id) continue;
          track.placementBeats = track.importedPlacementBeats;
          track.trimStartSeconds = track.importedTrimStartSeconds;
          track.trimEndSeconds = track.importedTrimEndSeconds;
          track.sourceBpm = track.importedSourceBpm;
          track.downbeatSeconds = track.importedDownbeatSeconds;
        }
      }
    } else {
      auto* track = find(project.tracks, value.trackId);
      if (!track) return missing();
      if constexpr (std::is_same_v<Command, MoveTrack>) {
        const auto members = std::count_if(project.tracks.begin(), project.tracks.end(),
            [&](const auto& candidate) { return candidate.timingGroupId == track->timingGroupId; });
        if (members > 1) return invalid("Unlink a track before moving it independently.");
        track->placementBeats += value.deltaBeats;
      } else if constexpr (std::is_same_v<Command, UnlinkTrack>) {
        if (value.newGroupId.empty() || find(project.groups, value.newGroupId))
          return invalid("Unlink requires a new, nonempty group ID.");
        const auto* original = find(project.groups, track->timingGroupId);
        if (!original) return missing();
        auto newGroup = *original;
        newGroup.id = value.newGroupId;
        project.groups.push_back(std::move(newGroup));
        track->timingGroupId = value.newGroupId;
      } else if constexpr (std::is_same_v<Command, TrimTrack>) {
        track->trimStartSeconds = value.startSeconds;
        track->trimEndSeconds = value.endSeconds;
      } else if constexpr (std::is_same_v<Command, SetMixer>) {
        track->gain = value.gain;
        track->pan = value.pan;
        track->mute = value.mute;
        track->solo = value.solo;
      }
    }
    return Result<void>::success();
  }, command);
}
}

ProjectHistory::ProjectHistory(Project initial, RevisionIdFactory revisionIds)
    : current_(std::move(initial)), revisionIds_(revisionIds ? std::move(revisionIds) : newRevisionId) {
  if (const auto result = validate(current_); !result) throw std::invalid_argument(result.error().message);
  seenRevisions_.insert(current_.revisionId);
  if (!current_.parentRevisionId.empty()) seenRevisions_.insert(current_.parentRevisionId);
}

Result<void> ProjectHistory::stampRevision(Project& snapshot) {
  const auto revision = revisionIds_();
  if (revision.empty() || seenRevisions_.contains(revision)) return invalid("Revision factory must produce a fresh, nonempty ID.");
  seenRevisions_.insert(revision);
  snapshot.parentRevisionId = current_.revisionId;
  snapshot.revisionId = revision;
  return Result<void>::success();
}

Result<void> ProjectHistory::apply(const ProjectCommand& command) {
  auto snapshot = current_;
  if (const auto result = edit(snapshot, command); !result) return result;
  return apply(std::move(snapshot));
}

Result<void> ProjectHistory::apply(Project snapshot) {
  if (snapshot.projectId != current_.projectId) return invalid("Snapshot belongs to another project.");
  if (snapshot.revisionId != current_.revisionId) return invalid("Snapshot was prepared from a stale revision.");
  if (const auto result = validate(snapshot); !result) return result;
  if (const auto result = stampRevision(snapshot); !result) return result;
  undo_.push_back(current_);
  current_ = std::move(snapshot);
  redo_.clear();
  return Result<void>::success();
}

Result<void> ProjectHistory::undo() {
  if (undo_.empty()) return invalid("Nothing to undo.");
  auto snapshot = undo_.back();
  if (const auto result = stampRevision(snapshot); !result) return result;
  redo_.push_back(current_);
  current_ = std::move(snapshot);
  undo_.pop_back();
  return Result<void>::success();
}

Result<void> ProjectHistory::redo() {
  if (redo_.empty()) return invalid("Nothing to redo.");
  auto snapshot = redo_.back();
  if (const auto result = stampRevision(snapshot); !result) return result;
  undo_.push_back(current_);
  current_ = std::move(snapshot);
  redo_.pop_back();
  return Result<void>::success();
}
} // namespace jeff::daw
