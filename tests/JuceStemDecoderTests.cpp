#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "stems/JuceStemDecoder.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <filesystem>
#include <fstream>
#include <chrono>

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
