#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "stems/WavExporter.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <unordered_map>

using namespace jeff::daw;

namespace {
class ByteInput final : public InputStream {
public:
  Result<std::size_t> read(std::span<std::byte> out) override {
    if(done_ || out.empty()) return Result<std::size_t>::success(0);
    out[0] = std::byte{0}; done_ = true;
    return Result<std::size_t>::success(1);
  }
private:
  bool done_ = false;
};

struct Pattern {
  int channels = 0;
  int rate = 0;
  Frame frames = 0;
  std::function<float(Frame, int)> sample;
};

class PatternReader final : public AudioSampleReader {
public:
  explicit PatternReader(Pattern pattern) : pattern_(std::move(pattern)) {
    metadata_.channels = pattern_.channels;
    metadata_.sourceRate = pattern_.rate;
    metadata_.frameCount = pattern_.frames;
  }
  const DecodedAudio& metadata() const noexcept override { return metadata_; }
  Result<void> read(Frame start, int frames, std::span<float* const> output,
                    CancellationToken& token) override {
    if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled, "cancelled");
    if(start < 0 || frames < 0 || start > metadata_.frameCount - frames ||
       output.size() != std::size_t(metadata_.channels))
      return Result<void>::failure(ErrorCode::decodeFailure, "invalid fixture read");
    for(int frame = 0; frame < frames; ++frame)
      for(int channel = 0; channel < metadata_.channels; ++channel)
        output[std::size_t(channel)][frame] = pattern_.sample(start + frame, channel);
    return Result<void>::success();
  }
private:
  Pattern pattern_;
  DecodedAudio metadata_;
};

class PatternDecoder final : public AudioDecoder {
public:
  void add(const std::filesystem::path& path, Pattern pattern) {
    patterns_[path.lexically_normal().string()] = std::move(pattern);
  }
  Result<DecodedAudio> inspect(const std::filesystem::path&, CancellationToken&,
                               std::size_t) override {
    return Result<DecodedAudio>::failure(ErrorCode::decodeFailure, "unused");
  }
  Result<std::unique_ptr<AudioSampleReader>> openReader(const std::filesystem::path& path,
                                                        CancellationToken&) override {
    auto found = patterns_.find(path.lexically_normal().string());
    if(found == patterns_.end())
      return Result<std::unique_ptr<AudioSampleReader>>::failure(ErrorCode::decodeFailure,
                                                                  "missing fixture");
    return Result<std::unique_ptr<AudioSampleReader>>::success(
        std::make_unique<PatternReader>(found->second));
  }
private:
  std::unordered_map<std::string, Pattern> patterns_;
};

struct ExportFixture {
  std::filesystem::path root;
  MediaStore store;
  PatternDecoder decoder;
  PreparedAudio prepared;

  ExportFixture()
      : root(std::filesystem::current_path() /
             ("jds-export-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))),
        store(root) {
    std::filesystem::create_directories(root);
    CancellationToken token;
    auto staged = store.stage({"impulses.wav", 1, [] {
      return Result<std::unique_ptr<InputStream>>::success(std::make_unique<ByteInput>());
    }}, token);
    REQUIRE(staged);
    auto asset = store.commit(staged.value(), 2, 48000, 24000);
    REQUIRE(asset);
    decoder.add(store.root() / asset.value().relativePath,
                {2, 48000, 24000, [](Frame frame, int channel) {
                  if(channel == 0 && frame == 4800) return 0.5f;
                  if(channel == 1 && frame == 9600) return 0.75f;
                  return 0.0f;
                }});
    StretchRenderer renderer(decoder);
    StretchOptions options;
    options.mode = TimingMode::preserve;
    options.renderRate = 48000;
    auto result = renderer.prepareGroup({StretchMember{asset.value()}}, preserveTimingMap(120),
                                        120, store, token, options);
    REQUIRE(result);
    prepared = std::move(result.value());
  }
  ~ExportFixture() {
    prepared = PreparedAudio{};
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  PlaybackSnapshot snapshot(bool firstSolo = false, bool secondMute = false) const {
    PlaybackTrack first;
    first.audio = prepared; first.firstChannel = 0; first.channels = 1; first.solo = firstSolo;
    PlaybackTrack second;
    second.audio = prepared; second.firstChannel = 1; second.channels = 1; second.mute = secondMute;
    return PlaybackSnapshot(120, {std::move(first), std::move(second)});
  }
};

class MemoryTarget final : public OutputTarget {
public:
  class Stream final : public StagedOutputStream {
  public:
    Stream(MemoryTarget& owner, std::string name) : owner_(owner), name_(std::move(name)) {}
    Result<void> write(std::span<const std::byte> bytes) override {
      const auto writeIndex = writes_++;
      if(owner_.cancelOnFirstWrite && writeIndex == 0 && owner_.token) owner_.token->cancel();
      if(owner_.failAfterWrites >= 0 && writes_ > owner_.failAfterWrites)
        return Result<void>::failure(ErrorCode::writeFailure, "simulated write failure");
      staging_.insert(staging_.end(), bytes.begin(), bytes.end());
      return Result<void>::success();
    }
    Result<void> commit() override {
      if(owner_.failCommit)
        return Result<void>::failure(ErrorCode::writeFailure, "simulated close failure");
      owner_.files[name_] = staging_;
      committed_ = true;
      return Result<void>::success();
    }
    void discard() noexcept override {
      ++owner_.discards;
      staging_.clear();
      if(committed_) owner_.files.erase(name_);
    }
  private:
    MemoryTarget& owner_;
    std::string name_;
    std::vector<std::byte> staging_;
    int writes_ = 0;
    bool committed_ = false;
  };

  Result<StagedOutput> openStaged(const std::string& preferred,
                                  OverwritePolicy policy) override {
    if(files.contains(preferred) && policy == OverwritePolicy::failIfExists)
      return Result<StagedOutput>::failure(ErrorCode::writeFailure, "destination exists");
    return Result<StagedOutput>::success(
        {preferred, std::make_unique<Stream>(*this, preferred)});
  }

  std::map<std::string, std::vector<std::byte>> files;
  int failAfterWrites = -1;
  bool failCommit = false;
  bool cancelOnFirstWrite = false;
  CancellationToken* token = nullptr;
  int discards = 0;
};

std::uint32_t read32(const std::vector<std::byte>& bytes, std::size_t offset) {
  return std::uint32_t(std::to_integer<unsigned>(bytes[offset])) |
         (std::uint32_t(std::to_integer<unsigned>(bytes[offset + 1])) << 8) |
         (std::uint32_t(std::to_integer<unsigned>(bytes[offset + 2])) << 16) |
         (std::uint32_t(std::to_integer<unsigned>(bytes[offset + 3])) << 24);
}

float sample24(const std::vector<std::byte>& wav, Frame frame, int channel) {
  const auto offset = std::size_t(44 + (frame * 2 + channel) * 3);
  std::int32_t value = std::int32_t(std::to_integer<unsigned>(wav[offset])) |
      (std::int32_t(std::to_integer<unsigned>(wav[offset + 1])) << 8) |
      (std::int32_t(std::to_integer<unsigned>(wav[offset + 2])) << 16);
  if(value & 0x800000) value |= ~0xffffff;
  return float(value) / 8388607.0f;
}

Frame strongestFrame(const std::vector<std::byte>& wav) {
  const auto frames = Frame(read32(wav, 40) / 6);
  Frame strongest = 0;
  float maximum = 0;
  for(Frame frame = 0; frame < frames; ++frame) {
    const auto value = std::abs(sample24(wav, frame, 0));
    if(value > maximum) { maximum = value; strongest = frame; }
  }
  return strongest;
}
} // namespace

TEST_CASE("ExportReimportsAligned") {
  ExportFixture fixture;
  MemoryTarget target;
  CancellationToken token;
  double progress = 0;
  auto result = WavExporter{}.exportTracks(fixture.snapshot(), {0, 1}, target, token,
                                            [&](double value) { progress = value; });
  REQUIRE(result);
  REQUIRE(result.value().files.size() == 2);
  REQUIRE(progress == Catch::Approx(1));
  REQUIRE(target.files.at("track-01.wav").size() == 44 + 24000 * 6);
  REQUIRE(read32(target.files.at("track-01.wav"), 24) == 48000);
  REQUIRE(strongestFrame(target.files.at("track-01.wav")) == 4800);
  REQUIRE(strongestFrame(target.files.at("track-02.wav")) == 9600);
  REQUIRE(sample24(target.files.at("track-01.wav"), 4800, 0) ==
          Catch::Approx(0.5f / std::sqrt(2.0f)).margin(0.000001));
}

TEST_CASE("mix export respects mute and solo while track export ignores solo") {
  ExportFixture fixture;
  CancellationToken token;
  MemoryTarget mixTarget;
  auto mix = WavExporter{}.exportMix(fixture.snapshot(true, false), {0, 1}, mixTarget, token);
  REQUIRE(mix);
  REQUIRE(strongestFrame(mixTarget.files.at("mix.wav")) == 4800);
  REQUIRE(std::abs(sample24(mixTarget.files.at("mix.wav"), 9600, 0)) < 0.000001f);

  MemoryTarget tracksTarget;
  auto tracks = WavExporter{}.exportTracks(fixture.snapshot(true, false), {0, 1},
                                            tracksTarget, token);
  REQUIRE(tracks);
  REQUIRE(strongestFrame(tracksTarget.files.at("track-02.wav")) == 9600);

  MemoryTarget mutedTarget;
  auto muted = WavExporter{}.exportTracks(fixture.snapshot(false, true), {0, 1},
                                           mutedTarget, token);
  REQUIRE(muted);
  REQUIRE(sample24(mutedTarget.files.at("track-02.wav"), 9600, 0) == Catch::Approx(0));
}

TEST_CASE("export requires overwrite consent and removes failed staged output") {
  ExportFixture fixture;
  MemoryTarget target;
  CancellationToken token;
  auto first = WavExporter{}.exportMix(fixture.snapshot(), {0, 1}, target, token);
  REQUIRE(first);
  auto collision = WavExporter{}.exportMix(fixture.snapshot(), {0, 1}, target, token);
  REQUIRE_FALSE(collision);
  REQUIRE(collision.error().code == ErrorCode::writeFailure);
  auto overwritten = WavExporter{}.exportMix(fixture.snapshot(), {0, 1}, target, token, {},
                                               OverwritePolicy::overwriteConfirmed);
  REQUIRE(overwritten);

  MemoryTarget failing;
  failing.failAfterWrites = 1;
  auto failed = WavExporter{}.exportMix(fixture.snapshot(), {0, 1}, failing, token);
  REQUIRE_FALSE(failed);
  REQUIRE(failing.files.empty());
  REQUIRE(failing.discards == 1);
}

TEST_CASE("cancelled and unclosed provider exports never become completed files") {
  ExportFixture fixture;
  CancellationToken token;
  MemoryTarget cancelled;
  cancelled.cancelOnFirstWrite = true;
  cancelled.token = &token;
  auto result = WavExporter{}.exportMix(fixture.snapshot(), {0, 1}, cancelled, token);
  REQUIRE_FALSE(result);
  REQUIRE(result.error().code == ErrorCode::cancelled);
  REQUIRE(cancelled.files.empty());
  REQUIRE(cancelled.discards == 1);

  CancellationToken closeToken;
  MemoryTarget unclosed;
  unclosed.failCommit = true;
  auto closeFailure = WavExporter{}.exportMix(fixture.snapshot(), {0, 1}, unclosed, closeToken);
  REQUIRE_FALSE(closeFailure);
  REQUIRE(unclosed.files.empty());
  REQUIRE(unclosed.discards == 1);
}

TEST_CASE("export reports and safely limits clipped samples") {
  ExportFixture fixture;
  PlaybackTrack loud;
  loud.audio = fixture.prepared;
  loud.firstChannel = 1;
  loud.channels = 1;
  loud.gain = 2.0f;
  PlaybackSnapshot snapshot(120, {std::move(loud)});
  MemoryTarget target;
  CancellationToken token;
  auto result = WavExporter{}.exportMix(snapshot, {0, 1}, target, token);
  REQUIRE(result);
  REQUIRE(result.value().clipped);
  REQUIRE(sample24(target.files.at("mix.wav"), 9600, 0) == Catch::Approx(1.0f));
}
