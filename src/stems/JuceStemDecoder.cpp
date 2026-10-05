#include "stems/JuceStemDecoder.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <limits>
#include <algorithm>

namespace jeff::daw {
namespace {
// JUCE PCM readers deliberately zero-pad a short source read and return true.
// Track short reads during sample decoding so truncated originals aren't accepted.
// Compressed codecs may legitimately request a final partial byte block.
class CheckedFileStream final : public juce::InputStream {
public:
  explicit CheckedFileStream(const juce::File& file) : file_(file) {}
  juce::int64 getTotalLength() override { return file_.getTotalLength(); }
  juce::int64 getPosition() override { return file_.getPosition(); }
  bool setPosition(juce::int64 p) override { return file_.setPosition(p); }
  bool isExhausted() override { return file_.isExhausted(); }
  int read(void* dest, int requested) override {
    const int got = file_.read(dest, requested);
    shortRead = shortRead || got < requested;
    return got;
  }
  bool failed() const { return file_.getStatus().failed(); }
  bool shortRead = false;
private:
  juce::FileInputStream file_;
};
}
Result<DecodedAudio> JuceStemDecoder::inspect(const std::filesystem::path& path, CancellationToken& token, std::size_t maxBins) {
  using R = Result<DecodedAudio>;
  if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "Decode cancelled.");
  if (maxBins == 0 || maxBins > 4096) return R::failure(ErrorCode::decodeFailure, "Waveform bin limit must be between 1 and 4096.");
  const auto utf8 = path.u8string();
  auto stream = std::make_unique<CheckedFileStream>(juce::File(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()), static_cast<int>(utf8.size()))));
  if (stream->failed()) return R::failure(ErrorCode::readFailure, "Cannot open the staged original audio.");
  auto* checked = stream.get();
  juce::AudioFormatManager formats;
  juce::WavAudioFormat wav; juce::AiffAudioFormat aiff;
  const auto wavName = wav.getFormatName(), aiffName = aiff.getFormatName();
  formats.registerFormat(new juce::WavAudioFormat, true);
  formats.registerFormat(new juce::AiffAudioFormat, false);
  formats.registerFormat(new juce::FlacAudioFormat, false);
  formats.registerFormat(new juce::MP3AudioFormat, false);
  std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
  if (!reader) return R::failure(ErrorCode::decodeFailure, "Unsupported or corrupt audio; expected WAV, AIFF, FLAC or MP3.");
  if (!std::isfinite(reader->sampleRate) || reader->sampleRate <= 0 || reader->sampleRate > std::numeric_limits<int>::max() ||
      std::floor(reader->sampleRate) != reader->sampleRate || reader->numChannels == 0 || reader->numChannels > 64 || reader->lengthInSamples <= 0)
    return R::failure(ErrorCode::decodeFailure, "Audio has invalid source metadata or unsupported channel/sample-rate values.");
  const bool pcm = reader->getFormatName() == wavName || reader->getFormatName() == aiffName;
  DecodedAudio audio; audio.channels = static_cast<int>(reader->numChannels); audio.sourceRate = static_cast<int>(reader->sampleRate); audio.frameCount = reader->lengthInSamples;
  const auto bins = std::min<std::size_t>(maxBins, static_cast<std::size_t>(audio.frameCount));
  audio.waveform.resize(bins);
  for (auto& b : audio.waveform) { b.minimum = std::numeric_limits<float>::infinity(); b.maximum = -std::numeric_limits<float>::infinity(); }
  std::vector<double> squares(bins, 0), counts(bins, 0);
  constexpr int blockFrames = 4096;
  juce::AudioBuffer<float> block(audio.channels, blockFrames);
  for (Frame start = 0; start < audio.frameCount;) {
    if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "Decode cancelled.");
    const int frames = static_cast<int>(std::min<Frame>(blockFrames, audio.frameCount - start));
    checked->shortRead = false;
    if (!reader->read(block.getArrayOfWritePointers(), audio.channels, start, frames) || checked->failed() || (pcm && checked->shortRead))
      return R::failure(ErrorCode::decodeFailure, "Audio data is incomplete or decoding failed.");
    for (int f = 0; f < frames; ++f) {
      const auto bin = std::min(bins - 1, static_cast<std::size_t>(static_cast<long double>(start + f) * bins / audio.frameCount));
      auto& summary = audio.waveform[bin];
      for (int c = 0; c < audio.channels; ++c) {
        const auto sample = block.getSample(c, f);
        if (!std::isfinite(sample)) return R::failure(ErrorCode::decodeFailure, "Audio contains nonfinite samples.");
        summary.minimum = std::min(summary.minimum, sample); summary.maximum = std::max(summary.maximum, sample);
        squares[bin] += static_cast<double>(sample) * sample; counts[bin] += 1;
      }
    }
    start += frames;
  }
  if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "Decode cancelled.");
  for (std::size_t i = 0; i < bins; ++i) audio.waveform[i].rms = static_cast<float>(std::sqrt(squares[i] / counts[i]));
  return R::success(std::move(audio));
}
} // namespace jeff::daw
