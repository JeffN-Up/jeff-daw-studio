#pragma once

#include "project/MediaStore.h"
#include "project/ProjectCommands.h"

namespace jeff::daw {

// Summary combines all channels; bin boundaries partition source frames uniformly.
struct WaveformBin { float minimum = 0, maximum = 0, rms = 0; };
struct DecodedAudio {
  int channels = 0;
  int sourceRate = 0;
  Frame frameCount = 0;
  std::vector<WaveformBin> waveform;
};
class AudioDecoder {
public:
  virtual ~AudioDecoder() = default;
  virtual Result<DecodedAudio> inspect(const std::filesystem::path& stagedOriginal,
                                       CancellationToken&, std::size_t maxWaveformBins) = 0;
};
enum class ImportPhase { copying, decoding, committing };
struct ImportProgress {
  std::size_t fileIndex = 0, fileCount = 0;
  std::string displayName;
  std::uint64_t bytesCopied = 0;
  std::optional<std::uint64_t> expectedBytes;
  ImportPhase phase = ImportPhase::copying;
};
using ProgressCallback = std::function<void(const ImportProgress&)>;
struct ImportedFile {
  std::string displayName;
  std::optional<AudioAsset> asset;
  std::optional<Error> error;
  std::vector<WaveformBin> waveform;
  std::optional<PendingStagedCleanup> unresolvedStaging;
};
struct ImportReport {
  std::vector<ImportedFile> files;
  bool cancelled = false;
};
struct StemImportLimits {
  std::size_t maxFiles = 256;
  std::size_t maxWaveformBins = 1024;
};
// Blocking background-worker operation. One file is copied/decoded at a time;
// memory is bounded by stream/decoder blocks plus capped waveform summaries.
// No project mutation, UI thread access, or audio callback use.
class StemImporter {
public:
  explicit StemImporter(AudioDecoder& decoder, StemImportLimits limits = {});
  ImportReport import(const std::vector<InputStreamFactory>&, MediaStore&,
                      CancellationToken&, ProgressCallback = {});
private:
  AudioDecoder& decoder_;
  StemImportLimits limits_;
};

// Off-audio-thread batch command: successful files become linked Preserve tracks
// with full source bounds and a common insertion position; failures are skipped.
// One validated history snapshot gives one undo step and a fresh revision.
Result<void> applyImportedBatch(const ImportReport&, ProjectHistory&, double insertionBeats = 0);
} // namespace jeff::daw
