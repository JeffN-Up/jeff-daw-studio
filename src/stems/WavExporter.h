#pragma once

#include "stems/PlaybackSnapshot.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace jeff::daw {

struct ExportRange {
  double startBeat = 0;
  double endBeat = 0;
};

enum class OverwritePolicy { failIfExists, overwriteConfirmed };

class StagedOutputStream {
public:
  virtual ~StagedOutputStream() = default;
  virtual Result<void> write(std::span<const std::byte> bytes) = 0;
  // commit must not report success until the destination has flushed and closed.
  virtual Result<void> commit() = 0;
  virtual void discard() noexcept = 0;
};

struct StagedOutput {
  std::string name;
  std::unique_ptr<StagedOutputStream> stream;
};

class OutputTarget {
public:
  virtual ~OutputTarget() = default;
  // Implementations create a temporary destination and enforce the requested
  // collision policy. Android adapters may return a content-provider stream.
  virtual Result<StagedOutput> openStaged(const std::string& preferredName,
                                           OverwritePolicy policy) = 0;
};

struct ExportedFile {
  std::string name;
  Frame frames = 0;
};

struct ExportReport {
  std::vector<ExportedFile> files;
  bool clipped = false;
};

using ExportProgressCallback = std::function<void(double)>;

class WavExporter {
public:
  static constexpr int sampleRate = 48000;
  static constexpr int bitsPerSample = 24;

  Result<ExportReport> exportMix(const PlaybackSnapshot&, ExportRange, OutputTarget&,
                                 CancellationToken&, ExportProgressCallback = {},
                                 OverwritePolicy = OverwritePolicy::failIfExists) const;
  Result<ExportReport> exportTracks(const PlaybackSnapshot&, ExportRange, OutputTarget&,
                                    CancellationToken&, ExportProgressCallback = {},
                                    OverwritePolicy = OverwritePolicy::failIfExists) const;
};

} // namespace jeff::daw
