#pragma once
#include "stems/StemImporter.h"

namespace jeff::daw {
struct StretchOptions {
  TimingMode mode = TimingMode::preserve;
  int renderRate = 0; // Zero selects the highest member source rate.
  int blockFrames = 4096;
  int maxChannels = 64;
  double maxDurationSeconds = 8 * 60 * 60;
  std::uint64_t maxPreparedBytes = 1024ULL * 1024 * 1024;
  std::uint64_t reserveBytes = 16ULL * 1024 * 1024;
};
// All offsets/trims are source seconds on one common grid. Trimmed regions are
// silent; the original group origin/duration is retained. Mixed rates are
// converted to renderRate with bounded linear interpolation before stretching.
struct StretchMember {
  AudioAsset asset;
  double sourceOffsetSeconds = 0;
  double trimStartSeconds = 0;
  double trimEndSeconds = 0; // Zero means source end.
  Id trackId;
};
struct PreparedMember {
  Id assetId, trackId;
  int firstChannel = 0, channels = 0;
};
class PreparedAudio {
public:
  int channels = 0, sampleRate = 0;
  Frame frameCount = 0;
  // Beat position of frame zero; frameCount/sampleRate is always seconds.
  double originBeats = 0;
  std::vector<PreparedMember> members;
  // Immutable interleaved float32 cache, owned until the last worker handle
  // retires. Copy/destruction/read belong to background owners; callbacks use
  // read-ahead buffers, never this type. No renderer-retained cache/history.
  const std::filesystem::path& dataPath() const noexcept;
  Result<void> read(Frame start, int frames, std::span<float* const>, CancellationToken&) const;
private:
  struct Storage;
  std::shared_ptr<Storage> storage_;
  friend class StretchRenderer;
};
class StretchRenderer {
public:
  explicit StretchRenderer(AudioDecoder& decoder) : decoder_(decoder) {}
  // Map-only convenience explicitly means Edit. Preserve callers pass options
  // or use the project/group overload, which respects the persistent mode.
  Result<PreparedAudio> prepare(const AudioAsset&, const TimingMap&, double targetBpm,
                                MediaStore&, CancellationToken&);
  Result<PreparedAudio> prepare(const AudioAsset&, const TimingMap&, double targetBpm,
                                MediaStore&, CancellationToken&, const StretchOptions&);
  Result<PreparedAudio> prepareGroup(const std::vector<StretchMember>&, const TimingMap&,
                                    double targetBpm, MediaStore&, CancellationToken&,
                                    const StretchOptions& = {});
  Result<PreparedAudio> prepareGroup(const Project&, const Id& groupId, MediaStore&,
                                    CancellationToken&, const StretchOptions& = {});
private:
  AudioDecoder& decoder_;
};
} // namespace jeff::daw
