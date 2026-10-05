#pragma once
#include "stems/BeatAnalyzer.h"
#include "stems/StretchRenderer.h"

namespace jeff::daw {
struct ManualSyncTiming { double bpm = 0, downbeatSeconds = 0; };
struct SyncOptions {
  std::optional<Id> rhythmicGuideTrackId;
  std::optional<double> manualBpm;
  std::optional<double> manualDownbeatSeconds;
  std::optional<ManualSyncTiming> referenceCorrection;
};
struct SyncTrackTiming { Id trackId; double placementBeats = 0, downbeatSeconds = 0; };
struct SyncProposal {
  Id projectId, sourceRevision, groupId, guideTrackId;
  std::optional<Id> referenceTrackId;
  TimingMap timingMap;
  double placementDeltaBeats = 0;
  std::vector<SyncTrackTiming> memberTiming;
  double sourceBpm = 0, targetBpm = 0, downbeatSeconds = 0, confidence = 0;
  BeatEstimate guideEstimate;
  std::optional<BeatEstimate> referenceEstimate;
  bool requiresCorrection = true;
  std::string correctionReason;
  // Captures timing inputs as well as revision: callers may edit raw snapshots.
  Project sourceSnapshot;
};
// Temporary snapshots and cache handles belong to a background owner. Releasing
// this object cancels audition without entering history or changing the project.
struct SyncPreview { Project original, proposed; PreparedAudio originalAudio, proposedAudio; };
Result<Project> proposalSnapshot(const Project&, const SyncProposal&);
Result<void> applyProposal(const SyncProposal&, ProjectHistory&);
// Removal is one validated history snapshot and retains every applied sync map.
Result<void> removeTrackWithSyncRetention(const Id&, ProjectHistory&);
} // namespace jeff::daw
