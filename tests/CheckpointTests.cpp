#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "checkpoint/CheckpointService.h"
#include "project/ProjectSerializer.h"

#include <miniz.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>

using namespace jeff::daw;

namespace {
Project savedProject() {
  Project project;
  project.projectId = "portable-song";
  project.revisionId = "revision-2";
  project.parentRevisionId = "revision-1";
  project.tempoBpm = 98.5;
  project.playheadBeats = 12.25;
  project.assets.push_back({"asset-1", "media/asset-1/original", "abc123", 2, 48000, 480000});
  project.groups.push_back({"group-1", TimingMode::edit,
      {{{0.0, 0.0}, {2.0, 3.0}, {10.0, 16.0}}}, preserveTimingMap(120.0),
      "", false});
  Track track;
  track.id = "track-1";
  track.name = "Lead Vocal";
  track.assetId = "asset-1";
  track.timingGroupId = "group-1";
  track.placementBeats = 4.0;
  track.importedPlacementBeats = 1.0;
  track.trimStartSeconds = track.importedTrimStartSeconds = 0.25;
  track.trimEndSeconds = track.importedTrimEndSeconds = 8.0;
  track.gain = 0.75;
  track.pan = -0.2;
  track.mute = true;
  track.sourceBpm = track.importedSourceBpm = 120;
  track.downbeatSeconds = track.importedDownbeatSeconds = 0.5;
  project.tracks.push_back(track);
  project.extensions["futureProject"] = R"({"nested":[1,true,"ok"]})";
  project.assets[0].extensions["futureAsset"] = "17";
  project.groups[0].extensions["futureGroup"] = R"({"mode":"later"})";
  project.tracks[0].extensions["futureTrack"] = "[3,2,1]";
  return project;
}

struct TemporaryDirectory {
  std::filesystem::path path = std::filesystem::current_path() /
      ("jds-checkpoint-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { std::filesystem::create_directories(path); }
  ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

class VectorInput final : public InputStream {
public:
  explicit VectorInput(const std::vector<std::byte>& bytes, std::size_t chunk = 137)
      : bytes_(bytes), chunk_(chunk) {}
  Result<std::size_t> read(std::span<std::byte> destination) override {
    if(failAfter_ && offset_ >= *failAfter_)
      return Result<std::size_t>::failure(ErrorCode::readFailure, "simulated interrupted provider");
    const auto count = std::min({destination.size(), chunk_, bytes_.size() - offset_});
    std::copy_n(bytes_.data() + offset_, count, destination.data());
    offset_ += count;
    return Result<std::size_t>::success(count);
  }
  void failAfter(std::size_t bytes) { failAfter_ = bytes; }
private:
  const std::vector<std::byte>& bytes_;
  std::size_t chunk_;
  std::size_t offset_ = 0;
  std::optional<std::size_t> failAfter_;
};

class MemoryCheckpointTarget final : public OutputTarget {
public:
  class Stream final : public StagedOutputStream {
  public:
    Stream(MemoryCheckpointTarget& owner, std::string name)
        : owner_(owner), name_(std::move(name)) {}
    Result<void> write(std::span<const std::byte> bytes) override {
      if(owner_.failWrites) return Result<void>::failure(ErrorCode::writeFailure, "simulated output failure");
      staging_.insert(staging_.end(), bytes.begin(), bytes.end());
      return Result<void>::success();
    }
    Result<void> commit() override {
      owner_.files[name_] = std::move(staging_);
      return Result<void>::success();
    }
    void discard() noexcept override { ++owner_.discards; staging_.clear(); }
  private:
    MemoryCheckpointTarget& owner_;
    std::string name_;
    std::vector<std::byte> staging_;
  };
  Result<StagedOutput> openStaged(const std::string& preferred, OverwritePolicy policy) override {
    if(files.contains(preferred) && policy == OverwritePolicy::failIfExists)
      return Result<StagedOutput>::failure(ErrorCode::writeFailure, "destination exists");
    return Result<StagedOutput>::success({preferred, std::make_unique<Stream>(*this, preferred)});
  }
  std::map<std::string, std::vector<std::byte>> files;
  bool failWrites = false;
  int discards = 0;
};

void writeBytes(const std::filesystem::path& path, std::string_view bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), std::streamsize(bytes.size()));
  REQUIRE(output.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<std::byte> result;
  for(char value; input.get(value);) result.push_back(std::byte(static_cast<unsigned char>(value)));
  return result;
}

std::vector<std::byte> makeZip(const std::filesystem::path& path,
                               const std::vector<std::pair<std::string, std::string>>& entries) {
  mz_zip_archive zip{};
  REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
  for(const auto& [name, value] : entries)
    REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), value.data(), value.size(), MZ_NO_COMPRESSION));
  REQUIRE(mz_zip_writer_finalize_archive(&zip));
  REQUIRE(mz_zip_writer_end(&zip));
  return readBytes(path);
}

Project packageProject(std::string revision = "revision-b", std::string parent = "revision-a") {
  auto project = savedProject();
  project.revisionId = std::move(revision);
  project.parentRevisionId = std::move(parent);
  project.assets[0].checksum = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  return project;
}

std::vector<std::byte> exportPackage(const Project& project, const std::filesystem::path& sourceRoot) {
  writeBytes(sourceRoot / project.assets[0].relativePath, "abc");
  MediaStore media(sourceRoot);
  MemoryCheckpointTarget target;
  CancellationToken token;
  auto exported = CheckpointService{}.exportCheckpoint(project, media, target, token);
  REQUIRE(exported);
  REQUIRE(exported.value().revisionId == project.revisionId);
  REQUIRE(target.files.size() == 1);
  return target.files.begin()->second;
}
} // namespace

TEST_CASE("ProjectSerializerRoundTripPreservesStateAndUnknownFields") {
  const auto original = savedProject();
  auto text = serializeProject(original);
  REQUIRE(text);
  auto parsed = deserializeProject(text.value());
  REQUIRE(parsed);
  const auto& project = parsed.value();
  REQUIRE(project.projectId == original.projectId);
  REQUIRE(project.revisionId == original.revisionId);
  REQUIRE(project.parentRevisionId == original.parentRevisionId);
  REQUIRE(project.tempoBpm == Catch::Approx(98.5));
  REQUIRE(project.playheadBeats == Catch::Approx(12.25));
  REQUIRE(project.tracks[0].name == "Lead Vocal");
  REQUIRE(project.tracks[0].placementBeats == Catch::Approx(4));
  REQUIRE(project.tracks[0].gain == Catch::Approx(0.75));
  REQUIRE(project.groups[0].timingMap.markers[2].destinationBeats == Catch::Approx(16));
  REQUIRE(project.extensions == original.extensions);
  REQUIRE(project.assets[0].extensions == original.assets[0].extensions);
  REQUIRE(project.groups[0].extensions == original.groups[0].extensions);
  REQUIRE(project.tracks[0].extensions == original.tracks[0].extensions);
}

TEST_CASE("ProjectSaveIsAtomicAndUnsupportedSchemaDoesNotReplace") {
  TemporaryDirectory temporary;
  const auto path = temporary.path / "project.json";
  auto original = savedProject();
  REQUIRE(saveProject(original, path));
  auto loaded = loadProject(path);
  REQUIRE(loaded);
  REQUIRE(loaded.value().revisionId == "revision-2");

  auto invalid = original;
  invalid.schemaVersion = currentProjectSchemaVersion + 1;
  REQUIRE_FALSE(saveProject(invalid, path));
  loaded = loadProject(path);
  REQUIRE(loaded);
  REQUIRE(loaded.value().revisionId == "revision-2");

  auto newer = original;
  newer.revisionId = "revision-3";
  newer.parentRevisionId = "revision-2";
  REQUIRE(saveProject(newer, path));
  auto recovery = loadProject(temporary.path / "project.json.recovery");
  REQUIRE(recovery);
  REQUIRE(recovery.value().revisionId == "revision-2");

  std::ofstream unsupported(temporary.path / "unsupported.json", std::ios::binary);
  unsupported << R"({"schemaVersion":99,"projectId":"x","revisionId":"y","tempoBpm":120,"assets":[],"groups":[],"tracks":[]})";
  unsupported.close();
  auto rejected = loadProject(temporary.path / "unsupported.json");
  REQUIRE_FALSE(rejected);
  REQUIRE(rejected.error().code == ErrorCode::invalidProject);
}

TEST_CASE("MalformedAndOversizedProjectDocumentsAreRejected") {
  REQUIRE_FALSE(deserializeProject("not json"));
  REQUIRE_FALSE(deserializeProject("[]"));
  std::string oversized(16 * 1024 * 1024 + 1, ' ');
  auto result = deserializeProject(oversized);
  REQUIRE_FALSE(result);
  REQUIRE(result.error().code == ErrorCode::invalidProject);
}

TEST_CASE("CheckpointRoundTrip") {
  TemporaryDirectory temporary;
  const auto project = packageProject();
  const auto package = exportPackage(project, temporary.path / "source");
  VectorInput input(package);
  LocalProjectStore destination(temporary.path / "projects");
  CancellationToken token;
  auto imported = CheckpointService{}.importCheckpoint(input, destination, token);
  REQUIRE(imported);
  REQUIRE(imported.value().disposition == ImportDisposition::installed);
  REQUIRE(imported.value().project.revisionId == project.revisionId);
  auto reopened = loadProject(imported.value().projectRoot / "project.json");
  REQUIRE(reopened);
  REQUIRE(reopened.value().playheadBeats == Catch::Approx(project.playheadBeats));
  REQUIRE(reopened.value().tracks[0].gain == Catch::Approx(project.tracks[0].gain));
  REQUIRE(readBytes(imported.value().projectRoot / project.assets[0].relativePath) ==
          std::vector<std::byte>{std::byte{0x61}, std::byte{0x62}, std::byte{0x63}});

  VectorInput duplicate(package);
  auto again = CheckpointService{}.importCheckpoint(duplicate, destination, token);
  REQUIRE(again);
  REQUIRE(again.value().disposition == ImportDisposition::alreadyPresent);

  const auto updatedProject = packageProject("revision-c", "revision-b");
  const auto updatedPackage = exportPackage(updatedProject, temporary.path / "source-update");
  VectorInput updatedInput(updatedPackage);
  auto updated = CheckpointService{}.importCheckpoint(updatedInput, destination, token);
  REQUIRE(updated);
  REQUIRE(updated.value().disposition == ImportDisposition::updated);
  REQUIRE(updated.value().projectRoot == imported.value().projectRoot);
  auto previous = loadProject(updated.value().projectRoot.parent_path() /
                              (updated.value().projectRoot.filename().string() + ".recovery") /
                              "project.json");
  REQUIRE(previous);
  REQUIRE(previous.value().revisionId == "revision-b");
}

TEST_CASE("CheckpointConflictKeepsBoth") {
  TemporaryDirectory temporary;
  LocalProjectStore destination(temporary.path / "projects");
  CancellationToken token;
  const auto firstProject = packageProject("revision-b", "revision-a");
  const auto firstPackage = exportPackage(firstProject, temporary.path / "source-first");
  VectorInput firstInput(firstPackage);
  auto first = CheckpointService{}.importCheckpoint(firstInput, destination, token);
  REQUIRE(first);

  const auto conflictProject = packageProject("revision-c", "revision-a");
  const auto conflictPackage = exportPackage(conflictProject, temporary.path / "source-conflict");
  VectorInput conflictInput(conflictPackage);
  auto conflict = CheckpointService{}.importCheckpoint(conflictInput, destination, token);
  REQUIRE(conflict);
  REQUIRE(conflict.value().disposition == ImportDisposition::conflictCopy);
  REQUIRE(conflict.value().projectRoot != first.value().projectRoot);
  REQUIRE(std::filesystem::exists(first.value().projectRoot / "project.json"));
  REQUIRE(std::filesystem::exists(conflict.value().projectRoot / "project.json"));
}

TEST_CASE("UnsupportedSchemaDoesNotReplace") {
  TemporaryDirectory temporary;
  LocalProjectStore destination(temporary.path / "projects");
  CancellationToken token;
  const auto current = packageProject();
  const auto validPackage = exportPackage(current, temporary.path / "source");
  VectorInput validInput(validPackage);
  auto installed = CheckpointService{}.importCheckpoint(validInput, destination, token);
  REQUIRE(installed);

  const std::string unsupported = R"({"schemaVersion":99,"projectId":"portable-song","revisionId":"revision-z","parentRevisionId":"revision-b","tempoBpm":120,"playheadBeats":0,"assets":[],"groups":[],"tracks":[]})";
  const std::string manifest = R"({"format":"jeff-daw-checkpoint","schemaVersion":1,"projectId":"portable-song","revisionId":"revision-z","parentRevisionId":"revision-b","media":[]})";
  auto badPackage = makeZip(temporary.path / "unsupported.jdsproject",
                            {{"project.json", unsupported}, {"manifest.json", manifest}});
  VectorInput badInput(badPackage);
  auto rejected = CheckpointService{}.importCheckpoint(badInput, destination, token);
  REQUIRE_FALSE(rejected);
  auto stillCurrent = loadProject(installed.value().projectRoot / "project.json");
  REQUIRE(stillCurrent);
  REQUIRE(stillCurrent.value().revisionId == "revision-b");
}

TEST_CASE("PackageTraversalRejected") {
  TemporaryDirectory temporary;
  auto package = makeZip(temporary.path / "traversal.jdsproject",
                         {{"project.json", "{}"}, {"manifest.json", "{}"}, {"../escape", "owned"}});
  VectorInput input(package);
  LocalProjectStore destination(temporary.path / "projects");
  CancellationToken token;
  auto result = CheckpointService{}.importCheckpoint(input, destination, token);
  REQUIRE_FALSE(result);
  REQUIRE_FALSE(std::filesystem::exists(temporary.path / "escape"));
}

TEST_CASE("CheckpointChecksumMissingMediaAndInterruptedTransfersAreRejected") {
  TemporaryDirectory temporary;
  auto project = packageProject();
  project.assets[0].checksum = "deadbeef";
  auto projectText = serializeProject(project);
  REQUIRE(projectText);
  nlohmann::json manifest{{"format", "jeff-daw-checkpoint"}, {"schemaVersion", 1},
      {"projectId", project.projectId}, {"revisionId", project.revisionId},
      {"parentRevisionId", project.parentRevisionId}, {"media", nlohmann::json::array({{
        {"path", project.assets[0].relativePath}, {"sha256", "deadbeef"}, {"bytes", 3}}})}};
  auto checksumPackage = makeZip(temporary.path / "checksum.jdsproject",
      {{"project.json", projectText.value()}, {"manifest.json", manifest.dump()},
       {project.assets[0].relativePath, "abc"}});
  LocalProjectStore destination(temporary.path / "projects");
  CancellationToken token;
  VectorInput checksumInput(checksumPackage);
  REQUIRE_FALSE(CheckpointService{}.importCheckpoint(checksumInput, destination, token));

  auto missingPackage = makeZip(temporary.path / "missing.jdsproject",
      {{"project.json", projectText.value()}, {"manifest.json", manifest.dump()}});
  VectorInput missingInput(missingPackage);
  REQUIRE_FALSE(CheckpointService{}.importCheckpoint(missingInput, destination, token));

  const auto validPackage = exportPackage(packageProject(), temporary.path / "source");
  VectorInput interrupted(validPackage, 64);
  interrupted.failAfter(128);
  REQUIRE_FALSE(CheckpointService{}.importCheckpoint(interrupted, destination, token));
  REQUIRE_FALSE(std::filesystem::exists(destination.primaryPath(project.projectId)));
  for(const auto& entry : std::filesystem::directory_iterator(destination.root()))
    REQUIRE_FALSE(entry.path().filename().string().starts_with(".incoming-"));
}

TEST_CASE("CheckpointOutputFailureDiscardsTheStagedFile") {
  TemporaryDirectory temporary;
  const auto project = packageProject();
  writeBytes(temporary.path / "source" / project.assets[0].relativePath, "abc");
  MediaStore media(temporary.path / "source");
  MemoryCheckpointTarget target;
  target.failWrites = true;
  CancellationToken token;
  auto result = CheckpointService{}.exportCheckpoint(project, media, target, token);
  REQUIRE_FALSE(result);
  REQUIRE(target.files.empty());
  REQUIRE(target.discards == 1);
  const auto staging = temporary.path / "source" / ".checkpoint";
  REQUIRE(std::filesystem::exists(staging));
  REQUIRE(std::filesystem::directory_iterator(staging) == std::filesystem::directory_iterator{});
}
