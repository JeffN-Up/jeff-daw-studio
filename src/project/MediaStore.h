#pragma once

#include "project/Project.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

namespace jeff::daw {

// A provider may have no local path or seek support. A zero read means EOF;
// interrupted/failed reads must return an error, never EOF.
class InputStream {
public:
  virtual ~InputStream() = default;
  virtual Result<std::size_t> read(std::span<std::byte> destination) = 0;
};
struct InputStreamFactory {
  std::string displayName;
  std::optional<std::uint64_t> expectedBytes;
  std::function<Result<std::unique_ptr<InputStream>>()> open;
};

struct MediaStoreLimits {
  std::uint64_t maxFileBytes = 4ULL * 1024 * 1024 * 1024;
  std::uint64_t reserveBytes = 16ULL * 1024 * 1024;
};

class MediaStore {
public:
  // A stage exclusively owns its temporary directory, removed unless committed.
  class StagedMedia {
  public:
    StagedMedia(StagedMedia&&) noexcept;
    StagedMedia& operator=(StagedMedia&&) noexcept;
    ~StagedMedia();
    const std::filesystem::path& path() const noexcept { return path_; }
    const std::string& checksum() const noexcept { return checksum_; }
    std::uint64_t byteCount() const noexcept { return bytes_; }
  private:
    friend class MediaStore;
    StagedMedia(std::filesystem::path root, std::filesystem::path directory, Id id);
    void clean() noexcept;
    std::filesystem::path root_, directory_, path_;
    Id id_;
    std::string checksum_;
    std::uint64_t bytes_ = 0;
  };
  explicit MediaStore(std::filesystem::path projectRoot, MediaStoreLimits limits = {});
  const std::filesystem::path& root() const noexcept { return root_; }
  Result<StagedMedia> stage(const InputStreamFactory&, CancellationToken&,
                           std::function<void(std::uint64_t)> progress = {});
  // Rechecks staged byte identity before atomic publication; optional cancellation
  // is checked while hashing, avoiding an uninterruptible large-file commit.
  Result<AudioAsset> commit(StagedMedia&, int channels, int sourceRate, Frame frameCount,
                            CancellationToken* cancellation = nullptr);
  // Removes only the matching importer-owned directory under this store's root.
  // Call only for unreferenced assets, e.g. rolling back a cancelled import.
  Result<void> removeCommitted(const AudioAsset&);
private:
  std::filesystem::path root_;
  MediaStoreLimits limits_;
};
} // namespace jeff::daw
