#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "stems/JuceStemDecoder.h"
#include "stems/StretchRenderer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstring>

using namespace jeff::daw;
namespace {
struct NativeTemp {
  std::filesystem::path root = std::filesystem::current_path() / ("jds-native-decoder-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  NativeTemp() { std::filesystem::create_directories(root); }
  ~NativeTemp() { std::error_code ec; std::filesystem::remove_all(root, ec); }
};
juce::File file(const std::filesystem::path& path) { return juce::File(juce::String(path.wstring().c_str())); }
void writeFixture(juce::AudioFormat& format, const std::filesystem::path& path, int channels, int rate) {
  std::unique_ptr<juce::OutputStream> output = file(path).createOutputStream(); REQUIRE(output);
  const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(channels).withBitsPerSample(16);
  auto writer = format.createWriterFor(output, options); REQUIRE(writer);
  juce::AudioBuffer<float> buffer(channels, 8192); buffer.clear();
  for (int c = 0; c < channels; ++c) for (int f = 4096; f < 8192; ++f) buffer.setSample(c, f, c == 0 ? 0.5f : -0.5f);
  REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}
}

TEST_CASE("JuceDecoderReadsRealAiffAndFlac", "[stem-import][juce-decoder]") {
  NativeTemp temp; JuceStemDecoder decoder; CancellationToken token;
  juce::AiffAudioFormat aiff; juce::FlacAudioFormat flac;
  for (auto* format : std::vector<juce::AudioFormat*>{&aiff, &flac}) {
    INFO(format->getFormatName().toStdString());
    const auto path = temp.root / "audio-with-no-extension";
    writeFixture(*format, path, 2, 44100);
    auto result = decoder.inspect(path, token, 128); INFO((result ? "No error" : result.error().message)); REQUIRE(result);
    REQUIRE(result.value().channels == 2); REQUIRE(result.value().sourceRate == 44100); REQUIRE(result.value().frameCount == 8192);
    REQUIRE(result.value().waveform.size() == 128); REQUIRE(result.value().waveform.front().rms == 0);
    REQUIRE(result.value().waveform.back().minimum == Catch::Approx(-0.5)); REQUIRE(result.value().waveform.back().maximum == Catch::Approx(0.5));
    std::filesystem::remove(path);
  }
}
TEST_CASE("JuceDecoderReadsRealMp3", "[stem-import][juce-decoder]") {
  // JUCE's own shipped sound fixture is read from its pinned source dependency.
  // It is neither a substitute decoder nor music media shipped with this app.
  JuceStemDecoder decoder; CancellationToken token;
  const std::filesystem::path path = JDS_JUCE_MP3_FIXTURE; REQUIRE(std::filesystem::exists(path));
  auto result = decoder.inspect(path, token, 128); INFO((result ? "No error" : result.error().message)); REQUIRE(result);
  REQUIRE(result.value().channels > 0); REQUIRE(result.value().sourceRate > 0); REQUIRE(result.value().frameCount > 0);
  REQUIRE(result.value().waveform.size() <= 128);
  float peak = 0; for (const auto& b : result.value().waveform) peak = std::max(peak, b.rms);
  REQUIRE(peak > 0.001f);
}
TEST_CASE("JuceDecoderRejectsTruncatedPcmInsteadOfPaddingSilence", "[stem-import][juce-decoder]") {
  NativeTemp temp; JuceStemDecoder decoder; CancellationToken token; juce::WavAudioFormat wav;
  const auto path = temp.root / "truncated"; writeFixture(wav, path, 1, 48000);
  std::filesystem::resize_file(path, std::filesystem::file_size(path) - 32);
  auto result = decoder.inspect(path, token, 128);
  REQUIRE_FALSE(result); REQUIRE(result.error().code == ErrorCode::decodeFailure);
}

TEST_CASE("JuceSampleReaderKeepsOwnedSessionAndChecksRandomReads", "[stretch][juce-decoder]") {
  NativeTemp temp;JuceStemDecoder decoder;CancellationToken token;juce::WavAudioFormat wav;
  const auto path=temp.root/"samples";writeFixture(wav,path,2,48000);auto opened=decoder.openReader(path,token);REQUIRE(opened);
  auto& reader=*opened.value();REQUIRE(reader.metadata().frameCount==8192);juce::AudioBuffer<float> block(2,64);
  auto planes=std::span<float* const>(block.getArrayOfWritePointers(),2);
  REQUIRE(reader.read(6000,64,planes,token));REQUIRE(block.getSample(0,0)==Catch::Approx(.5));REQUIRE(block.getSample(1,0)==Catch::Approx(-.5));
  REQUIRE(reader.read(0,64,planes,token));REQUIRE(block.getSample(0,0)==0);
  auto outside=reader.read(8191,64,planes,token);REQUIRE_FALSE(outside);REQUIRE(outside.error().code==ErrorCode::decodeFailure);
  token.cancel();auto cancelled=reader.read(0,64,planes,token);REQUIRE_FALSE(cancelled);REQUIRE(cancelled.error().code==ErrorCode::cancelled);
}

namespace {
void writeIntegrityFlac(const std::filesystem::path& path, bool silent) {
  juce::FlacAudioFormat format;
  auto output = file(path).createOutputStream(); REQUIRE(output);
  const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(16);
  auto writer = format.createWriterFor(output, options); REQUIRE(writer);
  juce::AudioBuffer<float> buffer(2, 8192); buffer.clear();
  if (!silent) for (int f = 1024; f < 8192; ++f) {
    const auto sample = float((f * 37) % 1009 - 504) / 1024;
    buffer.setSample(0, f, sample); buffer.setSample(1, f, -sample);
  }
  REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, 8192));
}
std::string nativeFileBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
std::size_t flacAudioStart(const std::string& bytes) {
  REQUIRE(bytes.substr(0, 4) == "fLaC");
  std::size_t pos = 4;
  while (true) {
    REQUIRE(pos + 4 <= bytes.size());
    const auto flags = static_cast<unsigned char>(bytes[pos]);
    const auto length = (std::size_t(static_cast<unsigned char>(bytes[pos+1])) << 16)
                      | (std::size_t(static_cast<unsigned char>(bytes[pos+2])) << 8)
                      | std::size_t(static_cast<unsigned char>(bytes[pos+3]));
    pos += 4 + length; REQUIRE(pos <= bytes.size());
    if (flags & 0x80) return pos;
  }
}
std::uint64_t flacAdvertisedFrames(const std::string& bytes) {
  REQUIRE(bytes.size() >= 42); REQUIRE((static_cast<unsigned char>(bytes[4]) & 0x7f) == 0);
  std::uint64_t packed = 0;
  for (int i = 18; i < 26; ++i) packed = (packed << 8) | static_cast<unsigned char>(bytes[i]);
  return packed & 0xfffffffffULL;
}
class NativeMediaStream final : public jeff::daw::InputStream {
public:
  explicit NativeMediaStream(std::string bytes) : bytes_(std::move(bytes)) {}
  Result<std::size_t> read(std::span<std::byte> out) override {
    const auto n = std::min(out.size(), bytes_.size() - pos_);
    std::memcpy(out.data(), bytes_.data() + pos_, n); pos_ += n;
    return Result<std::size_t>::success(n);
  }
private: std::string bytes_; std::size_t pos_ = 0;
};
}

// Catches compressed EOF/CRC errors being accepted as JUCE's fabricated zeros.
// STREAMINFO remains intact; silence is independently a successful control.
TEST_CASE("JuceFlacIntegrityRejectsTruncationWithoutRejectingSilence", "[stretch][juce-decoder][flac-integrity]") {
  NativeTemp temp; JuceStemDecoder decoder; CancellationToken token;
  const auto validPath = temp.root / "valid-flac"; writeIntegrityFlac(validPath, false);
  const auto validBytes = nativeFileBytes(validPath); const auto audioStart = flacAudioStart(validBytes);
  REQUIRE(flacAdvertisedFrames(validBytes) == 8192); REQUIRE(validBytes.size() > audioStart + 10);
  auto truncatedBytes = validBytes.substr(0, audioStart + (validBytes.size() - audioStart) / 2);
  REQUIRE(truncatedBytes.substr(0, audioStart) == validBytes.substr(0, audioStart));
  REQUIRE(flacAdvertisedFrames(truncatedBytes) == 8192);
  const auto truncatedPath = temp.root / "truncated-flac";
  { std::ofstream out(truncatedPath, std::ios::binary); out.write(truncatedBytes.data(), truncatedBytes.size()); }
  juce::FlacAudioFormat format;
  auto permissive = std::unique_ptr<juce::AudioFormatReader>(format.createReaderFor(file(truncatedPath).createInputStream().release(), true));
  REQUIRE(permissive); REQUIRE(permissive->lengthInSamples == 8192); permissive.reset();
  SECTION("truncated reader and prepared render fail with no cache") {
    auto opened = decoder.openReader(truncatedPath, token);
    if (opened) {
      juce::AudioBuffer<float> block(2, 8192);
      auto decoded = opened.value()->read(0, 8192, std::span<float* const>(block.getArrayOfWritePointers(), 2), token);
      REQUIRE_FALSE(decoded); REQUIRE(decoded.error().code == ErrorCode::decodeFailure);
    } else REQUIRE(opened.error().code == ErrorCode::decodeFailure);
    MediaStore store(temp.root / "project");
    InputStreamFactory source{"truncated.flac", truncatedBytes.size(), [truncatedBytes] {
      return Result<std::unique_ptr<jeff::daw::InputStream>>::success(std::make_unique<NativeMediaStream>(truncatedBytes));
    }};
    auto staged = store.stage(source, token); REQUIRE(staged);
    auto asset = store.commit(staged.value(), 2, 48000, 8192); REQUIRE(asset);
    StretchRenderer renderer(decoder);
    auto prepared = renderer.prepare(asset.value(), {{{0,0},{1,2}}}, 120, store, token);
    REQUIRE_FALSE(prepared); REQUIRE(prepared.error().code == ErrorCode::decodeFailure);
    const auto renders = store.root() / ".renders";
    REQUIRE((!std::filesystem::exists(renders) || std::filesystem::is_empty(renders)));
    REQUIRE(nativeFileBytes(store.root() / asset.value().relativePath) == truncatedBytes);
  }
  SECTION("valid encoded audio and legitimate silence succeed") {
    const auto silentPath = temp.root / "silent-flac"; writeIntegrityFlac(silentPath, true);
    for (const auto& path : {validPath, silentPath}) {
      auto inspected = decoder.inspect(path, token, 128); INFO((inspected ? "OK" : inspected.error().message)); REQUIRE(inspected);
      REQUIRE(inspected.value().frameCount == 8192);
      auto opened = decoder.openReader(path, token); REQUIRE(opened);
      juce::AudioBuffer<float> block(2, 8192);
      REQUIRE(opened.value()->read(0, 8192, std::span<float* const>(block.getArrayOfWritePointers(), 2), token));
      if (path == silentPath) REQUIRE(block.getMagnitude(0, 8192) == 0);
      else REQUIRE(block.getMagnitude(0, 8192) > .1f);
    }
  }
}
