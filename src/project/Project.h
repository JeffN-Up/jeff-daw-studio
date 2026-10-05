#pragma once

#include <map>
#include <string>
#include <vector>
#include "project/Types.h"
#include "stems/TimingMap.h"

namespace jeff::daw {

constexpr int currentProjectSchemaVersion = 1;

// Extension values are opaque serialized JSON values. Persistence must copy them unchanged.
using ExtensionFields = std::map<std::string, std::string>;

struct AudioAsset {
  Id id;
  std::string relativePath;
  std::string checksum;
  int channels = 0;
  int sourceRate = 0;
  Frame frameCount = 0;
  ExtensionFields extensions;
};

enum class TimingMode { preserve, edit, autoSync };

struct TimingGroup {
  Id id;
  TimingMode mode = TimingMode::preserve;
  TimingMap timingMap;
  TimingMap importedTimingMap;
  Id syncReferenceTrackId;
  bool referenceUnavailable = false;
  ExtensionFields extensions;
};

struct Track {
  Id id;
  std::string name;
  Id assetId;
  Id timingGroupId;
  double placementBeats = 0.0;
  double importedPlacementBeats = 0.0;
  double trimStartSeconds = 0.0;
  double trimEndSeconds = 0.0;
  double importedTrimStartSeconds = 0.0;
  double importedTrimEndSeconds = 0.0;
  double gain = 1.0;
  double pan = 0.0;
  bool mute = false;
  bool solo = false;
  double sourceBpm = 0.0; // Zero means unknown.
  double downbeatSeconds = 0.0;
  double importedSourceBpm = 0.0;
  double importedDownbeatSeconds = 0.0;
  ExtensionFields extensions;
};

struct Project {
  int schemaVersion = currentProjectSchemaVersion;
  Id projectId;
  Id revisionId;
  Id parentRevisionId;
  double tempoBpm = 120.0;
  std::vector<Track> tracks;
  std::vector<AudioAsset> assets;
  std::vector<TimingGroup> groups;
  double playheadBeats = 0.0;
  ExtensionFields extensions;
};

Result<void> validate(const Project& project);

// Checked background source-grid contract shared by rendering and alignment.
// Group origin is its minimum track placement; Preserve follows project tempo.
Result<double> groupOriginBeats(const Project&, const Id& groupId);
Result<double> sourceGridOffset(const Project&, const Track&);
Result<double> sourceTimeToProjectBeat(const Project&, const Track&, double sourceSeconds);

} // namespace jeff::daw
