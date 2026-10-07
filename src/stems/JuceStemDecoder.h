#pragma once
#include "stems/StemImporter.h"

namespace jeff::daw {
// JUCE-backed worker adapter. Reads original staged media through small planar
// sample blocks, preserves source frame/rate metadata, and creates a bounded
// waveform. It owns no audio-device or UI state. MP3 must be enabled in CMake.
class JuceStemDecoder final : public AudioDecoder {
public:
  Result<DecodedAudio> inspect(const std::filesystem::path&, CancellationToken&, std::size_t maxWaveformBins) override;
  Result<std::unique_ptr<AudioSampleReader>> openReader(const std::filesystem::path&, CancellationToken&) override;
};
} // namespace jeff::daw
