#pragma once

#include "project/MediaStore.h"
#include "stems/WavExporter.h"
#include <filesystem>

namespace jeff::daw {

struct CheckpointLimits {
  std::uint64_t maxPackageBytes = 8ULL * 1024 * 1024 * 1024;
  std::uint64_t maxExtractedBytes = 8ULL * 1024 * 1024 * 1024;
  std::size_t maxEntries = 4096;
};

struct CheckpointInfo {
  std::string name;
  std::uint64_t bytes = 0;
  Id revisionId;
};

enum class ImportDisposition { installed, updated, alreadyPresent, conflictCopy };

struct ImportDecision {
  ImportDisposition disposition = ImportDisposition::installed;
  std::filesystem::path projectRoot;
  Project project;
};

class LocalProjectStore {
public:
  explicit LocalProjectStore(std::filesystem::path root, CheckpointLimits limits = {});
  const std::filesystem::path& root() const noexcept { return root_; }
  const CheckpointLimits& limits() const noexcept { return limits_; }
  std::filesystem::path primaryPath(const Id& projectId) const;
private:
  std::filesystem::path root_;
  CheckpointLimits limits_;
};

class CheckpointService {
public:
  explicit CheckpointService(CheckpointLimits limits = {}) : limits_(limits) {}
  Result<CheckpointInfo> exportCheckpoint(const Project&, MediaStore&, OutputTarget&,
                                          CancellationToken&, ExportProgressCallback = {},
                                          OverwritePolicy = OverwritePolicy::failIfExists) const;
  Result<ImportDecision> importCheckpoint(InputStream&, LocalProjectStore&,
                                          CancellationToken&) const;
private:
  CheckpointLimits limits_;
};

} // namespace jeff::daw
