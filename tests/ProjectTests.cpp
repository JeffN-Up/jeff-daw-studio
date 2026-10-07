#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <stdexcept>

#include "project/ProjectCommands.h"

using namespace jeff::daw;

namespace {
Project twoStemProject() {
  Project project;
  project.projectId = "song";
  project.revisionId = "revision-1";
  project.tempoBpm = 120.0;
  project.assets = {
      {"kick-media", "media/kick.wav", "hash-kick", 2, 48000, 480000},
      {"bass-media", "media/bass.wav", "hash-bass", 2, 44100, 441000},
  };
  project.groups.push_back(TimingGroup{"stems", TimingMode::preserve,
      preserveTimingMap(project.tempoBpm), preserveTimingMap(project.tempoBpm)});
  Track kick;
  kick.id = "kick";
  kick.name = "Kick";
  kick.assetId = "kick-media";
  kick.timingGroupId = "stems";
  kick.trimEndSeconds = kick.importedTrimEndSeconds = 10.0;
  Track bass = kick;
  bass.id = "bass";
  bass.name = "Bass";
  bass.assetId = "bass-media";
  project.tracks = {kick, bass};
  return project;
}
}

TEST_CASE("ProjectPreserveUndo", "[project]") {
  ProjectHistory history(twoStemProject());
  REQUIRE(history.current().tracks.size() == 2);
  const auto& before = history.current();
  const double first = before.tracks[0].placementBeats + mapTime(before.groups[0].timingMap, 0.5);
  const double second = before.tracks[1].placementBeats + mapTime(before.groups[0].timingMap, 1.5);
  REQUIRE(second - first == Catch::Approx(2.0));

  REQUIRE(history.apply(MoveGroup{"stems", 3.0}));
  const auto& moved = history.current();
  REQUIRE(moved.tracks[0].placementBeats + mapTime(moved.groups[0].timingMap, 0.5) == Catch::Approx(first + 3.0));
  REQUIRE(moved.tracks[1].placementBeats + mapTime(moved.groups[0].timingMap, 1.5) == Catch::Approx(second + 3.0));
  REQUIRE(history.undo());
  REQUIRE(history.current().tracks[0].placementBeats == Catch::Approx(0.0));
  REQUIRE(history.current().tracks[1].placementBeats == Catch::Approx(0.0));
  REQUIRE(history.redo());
  REQUIRE(history.current().tracks[1].placementBeats == Catch::Approx(3.0));
}

TEST_CASE("TimingRejectsCrossing", "[project]") {
  auto project = twoStemProject();
  for (const TimingMap invalid : {
           TimingMap{{{0.0, 0.0}, {2.0, 2.0}, {1.0, 3.0}}},
           TimingMap{{{0.0, 0.0}, {1.0, 2.0}, {1.0, 3.0}}},
           TimingMap{{{0.0, 0.0}, {1.0, 2.0}, {2.0, 1.0}}},
           TimingMap{{{0.0, 0.0}, {1.0, 0.0}}},
           TimingMap{{{-0.1, 0.0}, {1.0, 2.0}}},
           TimingMap{{{0.0, -1.0}, {1.0, 2.0}}},
           TimingMap{{{0.0, 0.0}, {std::numeric_limits<double>::quiet_NaN(), 2.0}}},
           TimingMap{{{0.0, 0.0}, {1.0, std::numeric_limits<double>::infinity()}}},
       }) {
    project.groups[0].timingMap = invalid;
    REQUIRE_FALSE(validate(project));
  }
  REQUIRE_FALSE(validate(TimingMap{}));
  REQUIRE_FALSE(validate(TimingMap{{{0.0, 0.0}}}));
}

TEST_CASE("linked edits require unlink and reset preserves mixer", "[project]") {
  ProjectHistory history(twoStemProject());
  REQUIRE_FALSE(history.apply(MoveTrack{"kick", 1.0}));
  REQUIRE(history.apply(UnlinkTrack{"kick", "kick-own-map"}));
  REQUIRE(history.apply(MoveTrack{"kick", 1.0}));
  REQUIRE(history.apply(TrimTrack{"kick", 0.25, 8.0}));
  REQUIRE(history.apply(SetMixer{"kick", 0.4, -0.5, true, false}));
  auto editedMap = preserveTimingMap(120.0);
  editedMap.markers[1].destinationBeats = 3.0;
  REQUIRE(history.apply(SetGroupTimingMap{"kick-own-map", editedMap}));
  REQUIRE(history.current().groups[1].timingMap.markers[1].destinationBeats == Catch::Approx(3.0));
  REQUIRE(history.current().groups[0].timingMap.markers[1].destinationBeats == Catch::Approx(2.0));
  REQUIRE(history.apply(ResetTiming{"kick-own-map"}));
  const auto& reset = history.current();
  REQUIRE(reset.tracks[0].placementBeats == Catch::Approx(0.0));
  REQUIRE(reset.tracks[0].trimStartSeconds == Catch::Approx(0.0));
  REQUIRE(reset.tracks[0].trimEndSeconds == Catch::Approx(10.0));
  REQUIRE(reset.tracks[0].gain == Catch::Approx(0.4));
  REQUIRE(reset.tracks[0].pan == Catch::Approx(-0.5));
  REQUIRE(reset.tracks[0].mute);
  REQUIRE(reset.tracks[1].trimEndSeconds == Catch::Approx(10.0));
  REQUIRE(reset.groups[1].timingMap.markers[1].destinationBeats == Catch::Approx(2.0));
}

TEST_CASE("TimingInterpolatesAndExtrapolates", "[project]") {
  const TimingMap map{{{0.0, 0.0}, {1.0, 2.0}, {3.0, 3.0}}};
  REQUIRE(validate(map));
  REQUIRE(mapTime(map, 0.5) == Catch::Approx(1.0));
  REQUIRE(mapTime(map, 1.0) == Catch::Approx(2.0));
  REQUIRE(mapTime(map, 2.0) == Catch::Approx(2.5));
  REQUIRE(mapTime(map, 5.0) == Catch::Approx(4.0));
}

TEST_CASE("TimingMapKeepsFiniteExtremeEndpoints", "[project]") {
  const auto maximum = std::numeric_limits<double>::max();
  const TimingMap map{{{0.0, 0.0}, {2.0, maximum}}};
  REQUIRE(validate(map));
  REQUIRE(mapTime(map, 2.0) == maximum);
  REQUIRE(mapTime(map, 1.0) == maximum / 2.0);
  const TimingMap wideMap{{{0.0, 0.0}, {maximum, maximum}}};
  REQUIRE(mapTime(wideMap, maximum / 2.0) == maximum / 2.0);
}

TEST_CASE("TimingMapRejectsInvalidQueriesAndMaps", "[project]") {
  const TimingMap valid{{{0.0, 0.0}, {1.0, 2.0}}};
  REQUIRE_THROWS_AS(mapTime(TimingMap{}, 1.0), std::invalid_argument);
  REQUIRE_THROWS_AS(mapTime(TimingMap{{{0.0, 0.0}}}, 1.0), std::invalid_argument);
  REQUIRE_THROWS_AS(mapTime(TimingMap{{{0.0, 0.0}, {1.0, 0.0}}}, 0.5), std::invalid_argument);
  REQUIRE_THROWS_AS(mapTime(valid, std::numeric_limits<double>::infinity()), std::invalid_argument);
  REQUIRE_THROWS_AS(mapTime(valid, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}

TEST_CASE("TimingMapReportsExtrapolationOverflow", "[project]") {
  const TimingMap map{{{0.0, 0.0}, {2.0, std::numeric_limits<double>::max()}}};
  REQUIRE_THROWS_AS(mapTime(map, 3.0), std::overflow_error);
}

TEST_CASE("ProjectEditsAreAtomicAndKeepRedoOnFailure", "[project]") {
  ProjectHistory history(twoStemProject());
  REQUIRE(history.apply(MoveGroup{"stems", 2.0}));
  REQUIRE(history.undo());
  const auto revision = history.current().revisionId;
  REQUIRE_FALSE(history.apply(TrimTrack{"kick", 8.0, 2.0}));
  REQUIRE_FALSE(history.apply(TrimTrack{"kick", 0.0, 11.0}));
  REQUIRE_FALSE(history.apply(SetMixer{"kick", 1.0, 2.0, false, false}));
  REQUIRE_FALSE(history.apply(MoveGroup{"missing", 1.0}));
  REQUIRE_FALSE(history.apply(UnlinkTrack{"kick", "stems"}));
  REQUIRE_FALSE(history.apply(SetGroupTimingMap{"stems", {{{0.0, 0.0}, {1.0, 0.0}}}}));
  REQUIRE(history.current().revisionId == revision);
  REQUIRE(history.current().tracks[0].trimEndSeconds == Catch::Approx(10.0));
  REQUIRE(history.current().groups.size() == 1);
  REQUIRE(history.redo());
  REQUIRE(history.current().tracks[0].placementBeats == Catch::Approx(2.0));
  REQUIRE(history.undo());
  REQUIRE(history.apply(SetMixer{"kick", 0.5, 0.0, false, true}));
  REQUIRE_FALSE(history.redo());
}

TEST_CASE("SharedTimingEditsAndResetKeepSourceMedia", "[project]") {
  auto project = twoStemProject();
  project.extensions["future-field"] = "{\"opaque\":[1,2]}";
  ProjectHistory history(project);
  REQUIRE(history.apply(SetGroupTimingMap{"stems", {{{0.0, 0.0}, {2.0, 6.0}}}}));
  REQUIRE(history.current().tracks[0].timingGroupId == history.current().tracks[1].timingGroupId);
  REQUIRE(mapTime(history.current().groups[0].timingMap, 1.5) == Catch::Approx(4.5));
  REQUIRE(history.apply(TrimTrack{"kick", 0.5, 9.0}));
  REQUIRE(history.current().assets[0].frameCount == 480000);
  REQUIRE(history.current().tracks[1].trimStartSeconds == Catch::Approx(0.0));
  REQUIRE(history.apply(MoveGroup{"stems", 4.0}));
  REQUIRE(history.apply(ResetTiming{"stems"}));
  REQUIRE(history.current().tracks[0].trimStartSeconds == Catch::Approx(0.0));
  REQUIRE(history.current().tracks[0].placementBeats == Catch::Approx(0.0));
  REQUIRE(history.current().tracks[1].placementBeats == Catch::Approx(0.0));
  REQUIRE(history.current().groups[0].mode == TimingMode::preserve);
  REQUIRE(history.current().assets[0].relativePath == "media/kick.wav");
  REQUIRE(history.current().extensions == project.extensions);
}

TEST_CASE("ProjectValidationRejectsBrokenReferencesAndNonfiniteValues", "[project]") {
  const auto valid = twoStemProject();
  REQUIRE(validate(valid));
  auto broken = valid;
  broken.tracks[1].id = "kick";
  REQUIRE_FALSE(validate(broken));
  broken = valid;
  broken.tracks[0].assetId = "missing";
  REQUIRE_FALSE(validate(broken));
  broken = valid;
  broken.tracks[0].timingGroupId = "missing";
  REQUIRE_FALSE(validate(broken));
  broken = valid;
  broken.tempoBpm = std::numeric_limits<double>::quiet_NaN();
  REQUIRE_FALSE(validate(broken));
  broken = valid;
  broken.assets[0].sourceRate = 0;
  REQUIRE_FALSE(validate(broken));
  broken = valid;
  broken.tracks[0].trimEndSeconds = 11.0;
  REQUIRE_FALSE(validate(broken));
  broken = valid;
  broken.assets[0].relativePath = "../outside.wav";
  REQUIRE_FALSE(validate(broken));
}

TEST_CASE("ProjectSnapshotHistoryChangesRevisionAndPreservesExtensions", "[project]") {
  auto project = twoStemProject();
  project.tracks[0].extensions["future"] = "42";
  int revision = 1;
  ProjectHistory history(project, [&revision] { return "edit-" + std::to_string(revision++); });
  auto edited = history.current();
  edited.tempoBpm = 100.0;
  REQUIRE(history.apply(edited));
  REQUIRE(history.current().revisionId == "edit-1");
  REQUIRE(history.current().parentRevisionId == "revision-1");
  REQUIRE(history.undo());
  REQUIRE(history.current().tempoBpm == Catch::Approx(120.0));
  REQUIRE(history.current().revisionId == "edit-2");
  REQUIRE(history.current().parentRevisionId == "edit-1");
  REQUIRE(history.redo());
  REQUIRE(history.current().tempoBpm == Catch::Approx(100.0));
  REQUIRE(history.current().revisionId == "edit-3");
  REQUIRE(history.current().tracks[0].extensions == project.tracks[0].extensions);
  edited.projectId = "different-project";
  REQUIRE_FALSE(history.apply(edited));
  REQUIRE(history.current().revisionId == "edit-3");
}

TEST_CASE("ProjectRejectsStaleSnapshotsAndReusedRevisions", "[project]") {
  auto project = twoStemProject();
  int generated = 0;
  ProjectHistory history(project, [&generated] { return ++generated == 1 ? "new" : "revision-1"; });
  const auto stale = history.current();
  REQUIRE(history.apply(MoveGroup{"stems", 2.0}));
  REQUIRE_FALSE(history.apply(stale));
  REQUIRE(history.current().tracks[0].placementBeats == Catch::Approx(2.0));
  REQUIRE_FALSE(history.undo());
  REQUIRE(history.current().revisionId == "new");
}

TEST_CASE("ResetTimingRestoresImportedSourceTimingAndIsUndoable", "[project]") {
  auto project = twoStemProject();
  project.tracks[0].sourceBpm = project.tracks[0].importedSourceBpm = 90.0;
  project.tracks[0].downbeatSeconds = project.tracks[0].importedDownbeatSeconds = 0.5;
  ProjectHistory history(project);
  auto edited = history.current();
  edited.groups[0].mode = TimingMode::autoSync;
  edited.groups[0].syncReferenceTrackId = "bass";
  edited.tracks[0].sourceBpm = 100.0;
  edited.tracks[0].downbeatSeconds = 1.5;
  REQUIRE(history.apply(edited));
  REQUIRE(history.apply(ResetTiming{"stems"}));
  REQUIRE(history.current().tracks[0].sourceBpm == Catch::Approx(90.0));
  REQUIRE(history.current().tracks[0].downbeatSeconds == Catch::Approx(0.5));
  REQUIRE(history.current().groups[0].syncReferenceTrackId.empty());
  REQUIRE(history.undo());
  REQUIRE(history.current().tracks[0].sourceBpm == Catch::Approx(100.0));
  REQUIRE(history.current().groups[0].syncReferenceTrackId == "bass");
}
