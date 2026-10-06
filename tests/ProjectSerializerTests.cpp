#include "project/ProjectSerializer.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
using namespace jeff::daw;
TEST_CASE("Project document restores timing and mixer state", "[persistence]") {
  Project p;
  p.projectId = "song";
  p.revisionId = "edit";
  p.tempoBpm = 93.5;
  p.assets.push_back({"audio", "media/a.wav", "sha", 2, 48000, 96000, {{"future", "{\"x\":1}"}}});
  TimingGroup g;
  g.id = "group";
  g.timingMap = g.importedTimingMap = preserveTimingMap(p.tempoBpm);
  p.groups.push_back(g);
  Track t;
  t.id = "track";
  t.name = "Drums \"one\"";
  t.assetId = "audio";
  t.timingGroupId = "group";
  t.trimEndSeconds = t.importedTrimEndSeconds = 2;
  t.gain = .4;
  t.pan = -.3;
  t.solo = true;
  p.tracks.push_back(t);
  p.extensions["future"] = "[1,2]";
  auto encoded = serializeProject(p);
  REQUIRE(encoded);
  auto decoded = deserializeProject(encoded.value());
  REQUIRE(decoded);
  REQUIRE(decoded.value().tracks[0].name == t.name);
  REQUIRE(decoded.value().tracks[0].solo);
  REQUIRE(decoded.value().tracks[0].pan == t.pan);
  REQUIRE(decoded.value().assets[0].extensions == p.assets[0].extensions);
  REQUIRE(decoded.value().extensions == p.extensions);
}
TEST_CASE("Project document rejects future schema and malformed state", "[persistence]") {
  REQUIRE_FALSE(deserializeProject("{\"schemaVersion\":999}"));
  REQUIRE_FALSE(deserializeProject("not json"));
  REQUIRE_FALSE(deserializeProject("{}"));
}
TEST_CASE("Project save failure preserves the previous document", "[persistence]") {
  auto root = std::filesystem::temp_directory_path() / "jds-serializer-test";
  std::filesystem::create_directories(root);
  auto file = root / "project.json";
  Project p;
  p.projectId = "song";
  p.revisionId = "rev";
  REQUIRE(saveProject(file, p));
  p.tempoBpm = -1;
  REQUIRE_FALSE(saveProject(file, p));
  auto loaded = loadProject(file);
  REQUIRE(loaded);
  REQUIRE(loaded.value().tempoBpm == 120);
  std::filesystem::remove_all(root);
}
TEST_CASE("Project document rejects playheads outside the supported timeline", "[persistence]") {
  Project p;
  p.projectId = "song";
  p.revisionId = "rev";
  auto text = serializeProject(p);
  REQUIRE(text);
  auto replace = text.value();
  auto begin = replace.find("\"playheadBeats\": 0.0");
  REQUIRE(begin != std::string::npos);
  replace.replace(begin, std::string("\"playheadBeats\": 0.0").size(), "\"playheadBeats\": 1e100");
  REQUIRE_FALSE(deserializeProject(replace));
}
