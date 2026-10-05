#pragma once

#include <functional>
#include <unordered_set>
#include <variant>
#include "project/Project.h"

namespace jeff::daw {

struct MoveGroup { Id groupId; double deltaBeats; };
struct MoveTrack { Id trackId; double deltaBeats; };
struct UnlinkTrack { Id trackId; Id newGroupId; };
struct TrimTrack { Id trackId; double startSeconds; double endSeconds; };
struct SetMixer { Id trackId; double gain; double pan; bool mute; bool solo; };
struct SetGroupTimingMap { Id groupId; TimingMap map; TimingMode mode = TimingMode::edit; };
struct ResetTiming { Id groupId; };
using ProjectCommand = std::variant<MoveGroup, MoveTrack, UnlinkTrack, TrimTrack, SetMixer, SetGroupTimingMap, ResetTiming>;

// UI/background command boundary, never called from the audio callback.
// Each successful apply/undo/redo gets a fresh revision and keeps opaque extensions.
class ProjectHistory {
public:
  using RevisionIdFactory = std::function<Id()>;
  // Throws invalid_argument for an invalid initial project.
  explicit ProjectHistory(Project initial, RevisionIdFactory revisionIds = {});
  const Project& current() const noexcept { return current_; }
  Result<void> apply(const ProjectCommand& command);
  // Validated snapshot entry point for import/removal and later sync commands.
  // A snapshot must belong to the same project and current revision.
  // Opening a project creates a new history.
  Result<void> apply(Project snapshot);
  Result<void> undo();
  Result<void> redo();

private:
  Result<void> stampRevision(Project& snapshot);
  Project current_;
  std::vector<Project> undo_;
  std::vector<Project> redo_;
  RevisionIdFactory revisionIds_;
  std::unordered_set<Id> seenRevisions_;
};
} // namespace jeff::daw
