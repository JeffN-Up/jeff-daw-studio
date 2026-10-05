#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "stems/StemImporter.h"
#include "project/ProjectCommands.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef JDS_TEST_JUCE_IMPORT
#include "stems/JuceStemDecoder.h"
#endif

using namespace jeff::daw;
namespace {
// Actual little-endian PCM fixture: leading silence followed by opposing stereo samples.
void put16(std::string& b, unsigned v) { b += char(v); b += char(v >> 8); }
void put32(std::string& b, unsigned v) { put16(b, v); put16(b, v >> 16); }
std::string wav(int channels = 2, int rate = 48000) {
  const unsigned frames = 8192, bytes = frames * channels * 2;
  std::string b = "RIFF"; put32(b, 36 + bytes); b += "WAVEfmt "; put32(b, 16);
  put16(b, 1); put16(b, channels); put32(b, rate); put32(b, rate * channels * 2);
  put16(b, channels * 2); put16(b, 16); b += "data"; put32(b, bytes);
  for (unsigned f = 0; f < frames; ++f)
    for (int c = 0; c < channels; ++c) put16(b, f < 4096 ? 0 : (c == 0 ? 16384 : unsigned(-16384)));
  return b;
}
struct Temp {
  std::filesystem::path root;
  Temp() { static std::atomic<unsigned> count{0}; root = std::filesystem::current_path() / ("jds-import-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(count++)); std::filesystem::create_directories(root); }
  ~Temp() { std::error_code ec; std::filesystem::remove_all(root, ec); }
};
std::string readAll(const std::filesystem::path& p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
class ProviderStream : public InputStream {
public:
  ProviderStream(std::string bytes, bool fail) : bytes_(std::move(bytes)), fail_(fail) {}
  Result<std::size_t> read(std::span<std::byte> out) override {
    if (fail_ && position_ >= 17) return Result<std::size_t>::failure(ErrorCode::readFailure, "Provider interrupted.");
    const auto n = std::min({out.size(), bytes_.size() - position_, std::size_t(17)});
    std::memcpy(out.data(), bytes_.data() + position_, n); position_ += n;
    return Result<std::size_t>::success(n);
  }
private: std::string bytes_; std::size_t position_ = 0; bool fail_;
};
InputStreamFactory source(std::string name, std::string bytes, bool fail = false) {
  return {std::move(name), bytes.size(), [bytes = std::move(bytes), fail] {
    return Result<std::unique_ptr<InputStream>>::success(std::make_unique<ProviderStream>(bytes, fail));
  }};
}
#ifndef JDS_TEST_JUCE_IMPORT
// Test-only decoder parses real PCM WAV bytes. Portable tests prove orchestration/media
// ownership; supported production formats are tested only with the JUCE adapter target.
class FixtureDecoder : public AudioDecoder {
public:
  Result<DecodedAudio> inspect(const std::filesystem::path& p, CancellationToken& token, std::size_t bins) override {
    const auto b = readAll(p);
    auto u16 = [&](std::size_t n) { return unsigned(static_cast<unsigned char>(b[n])) | unsigned(static_cast<unsigned char>(b[n+1])) << 8; };
    auto u32 = [&](std::size_t n) { return u16(n) | u16(n+2) << 16; };
    if (b.size() < 44 || b.substr(0, 4) != "RIFF" || b.substr(8, 4) != "WAVE" || u16(20) != 1 || u16(34) != 16 || u16(22) == 0 || b.size() != 44 + u32(40))
      return Result<DecodedAudio>::failure(ErrorCode::decodeFailure, "Invalid fixture WAV.");
    DecodedAudio audio; audio.channels = int(u16(22)); audio.sourceRate = int(u32(24)); audio.frameCount = u32(40) / (audio.channels * 2);
    audio.waveform.resize(std::min<std::size_t>(bins, audio.frameCount));
    std::vector<std::size_t> counts(audio.waveform.size());
    for (Frame f = 0; f < audio.frameCount; ++f) {
      if (token.isCancelled()) return Result<DecodedAudio>::failure(ErrorCode::cancelled, "Cancelled.");
      auto bin = std::size_t(f) * audio.waveform.size() / std::size_t(audio.frameCount);
      for (int c = 0; c < audio.channels; ++c) {
        const float sample = static_cast<std::int16_t>(u16(44 + (std::size_t(f) * audio.channels + c) * 2)) / 32768.0f;
        auto& w = audio.waveform[bin]; w.minimum = std::min(w.minimum, sample); w.maximum = std::max(w.maximum, sample); w.rms += sample * sample; ++counts[bin];
      }
    }
    for (std::size_t i = 0; i < audio.waveform.size(); ++i) audio.waveform[i].rms = std::sqrt(audio.waveform[i].rms / counts[i]);
    return Result<DecodedAudio>::success(std::move(audio));
  }
};
using TestDecoder = FixtureDecoder;
#else
using TestDecoder = JuceStemDecoder;
#endif
std::size_t entries(const std::filesystem::path& p) { return std::filesystem::exists(p) ? std::distance(std::filesystem::directory_iterator(p), std::filesystem::directory_iterator{}) : 0; }
class PreventDeletion {
public:
  void hold(const std::filesystem::path& original) {
#ifdef _WIN32
    handle_ = CreateFileW(original.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(handle_ != INVALID_HANDLE_VALUE);
#else
    directory_ = original.parent_path();
    oldPermissions_ = std::filesystem::status(directory_).permissions();
    std::filesystem::permissions(directory_, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
#endif
  }
  void release() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
#else
    if (!directory_.empty()) { std::filesystem::permissions(directory_, oldPermissions_); directory_.clear(); }
#endif
  }
  ~PreventDeletion() { release(); }
private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  std::filesystem::path directory_;
  std::filesystem::perms oldPermissions_{};
#endif
};
}

TEST_CASE("StemImportPreservesSilence", "[stem-import]") {
  Temp temp; const auto original = wav(); const auto path = temp.root / "external.wav";
  { std::ofstream out(path, std::ios::binary); out.write(original.data(), original.size()); }
  MediaStore store(temp.root / "project"); TestDecoder decoder; StemImporter importer(decoder); CancellationToken token;
  auto report = importer.import({source("stem.wav", readAll(path))}, store, token);
  REQUIRE_FALSE(report.cancelled); REQUIRE(report.files.size() == 1); INFO((report.files[0].error ? report.files[0].error->message : "No error")); REQUIRE(report.files[0].asset);
  const auto& file = report.files[0]; const auto& a = *file.asset;
  REQUIRE(a.channels == 2); REQUIRE(a.sourceRate == 48000); REQUIRE(a.frameCount == 8192);
  REQUIRE(a.checksum.size() == 64); REQUIRE(readAll(store.root() / a.relativePath) == original); REQUIRE(readAll(path) == original);
  REQUIRE(file.waveform.size() == 1024); REQUIRE(file.waveform.front().minimum == 0); REQUIRE(file.waveform.front().maximum == 0);
  REQUIRE(file.waveform.back().minimum == Catch::Approx(-0.5)); REQUIRE(file.waveform.back().maximum == Catch::Approx(0.5));
  REQUIRE(file.waveform.back().rms == Catch::Approx(0.5));
  Project p; p.projectId = "project"; p.revisionId = "revision"; p.tempoBpm = 120;
  ProjectHistory history(p); REQUIRE(applyImportedBatch(report, history, 3));
  REQUIRE(history.current().tracks.size() == 1); const auto& track = history.current().tracks[0];
  REQUIRE(track.placementBeats == 3); REQUIRE(track.trimStartSeconds == 0); REQUIRE(track.trimEndSeconds == Catch::Approx(8192.0 / 48000));
  REQUIRE(history.current().groups[0].mode == TimingMode::preserve); REQUIRE(history.undo()); REQUIRE(history.current().tracks.empty());
  REQUIRE(history.redo()); REQUIRE(history.current().tracks.size() == 1);
}
TEST_CASE("DuplicateNamesRemainDistinct", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); TestDecoder decoder; StemImporter importer(decoder); CancellationToken token;
  const auto a = wav(1, 44100), b = wav(2, 48000);
  auto report = importer.import({source("same.wav", a), source("same.wav", b), source("same.wav", a)}, store, token);
  REQUIRE(report.files.size() == 3); for (const auto& f : report.files) { INFO((f.error ? f.error->message : "No error")); REQUIRE(f.asset); }
  REQUIRE(report.files[0].asset->id != report.files[1].asset->id); REQUIRE(report.files[0].asset->id != report.files[2].asset->id);
  REQUIRE(report.files[0].asset->relativePath != report.files[1].asset->relativePath);
  REQUIRE(report.files[0].asset->checksum != report.files[1].asset->checksum); REQUIRE(report.files[0].asset->checksum == report.files[2].asset->checksum);
  REQUIRE(report.files[0].asset->channels == 1); REQUIRE(report.files[0].asset->sourceRate == 44100);
  Project p; p.projectId = "p"; p.revisionId = "r"; ProjectHistory history(p);
  REQUIRE(applyImportedBatch(report, history, 0)); REQUIRE(history.current().groups.size() == 1); REQUIRE(history.current().tracks.size() == 3);
}
TEST_CASE("ImportCancelNoPartialAssets", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); TestDecoder decoder; StemImporter importer(decoder); CancellationToken token;
  auto report = importer.import({source("first.wav", wav()), source("second.wav", wav())}, store, token,
    [&](const ImportProgress& progress) { if (progress.fileIndex == 1 && progress.bytesCopied > 0) token.cancel(); });
  REQUIRE(report.cancelled); for (const auto& f : report.files) REQUIRE_FALSE(f.asset);
  REQUIRE(entries(temp.root / "media") == 0); REQUIRE(entries(temp.root / ".import") == 0);
}
TEST_CASE("MixedValidCorruptImport", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); TestDecoder decoder; StemImporter importer(decoder); CancellationToken token;
  auto report = importer.import({source("ok.wav", wav()), source("corrupt.wav", "garbage"), source("last.wav", wav(1, 44100))}, store, token);
  REQUIRE(report.files.size() == 3); REQUIRE(report.files[0].asset); REQUIRE(report.files[1].error); REQUIRE_FALSE(report.files[1].asset); REQUIRE(report.files[2].asset);
  REQUIRE(entries(temp.root / "media") == 2); REQUIRE(entries(temp.root / ".import") == 0);
}
TEST_CASE("ProviderReadFailureAndStorageLimitLeaveNoMedia", "[stem-import]") {
  Temp temp; MediaStore store(temp.root, MediaStoreLimits{1024, 0}); TestDecoder decoder; StemImporter importer(decoder); CancellationToken token;
  auto fail = source("provider.wav", wav(), true); fail.expectedBytes.reset();
  auto large = source("large.wav", wav()); large.expectedBytes.reset();
  auto report = importer.import({fail, large}, store, token);
  REQUIRE(report.files[0].error->code == ErrorCode::readFailure); REQUIRE(report.files[1].error->code == ErrorCode::storageLimit);
  REQUIRE(entries(temp.root / "media") == 0); REQUIRE(entries(temp.root / ".import") == 0);
}
TEST_CASE("MediaChecksumUsesSha256", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); CancellationToken token;
  auto staged = store.stage(source("hash", "abc"), token);
  REQUIRE(staged); REQUIRE(staged.value().checksum() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  auto longVector = store.stage(source("hash", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), token);
  REQUIRE(longVector); REQUIRE(longVector.value().checksum() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
TEST_CASE("ProviderLengthMismatchDoesNotCommitPartialMedia", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); TestDecoder decoder; StemImporter importer(decoder); CancellationToken token;
  auto shortInput = source("short.wav", wav()); shortInput.expectedBytes = *shortInput.expectedBytes + 17;
  auto longInput = source("long.wav", wav()); longInput.expectedBytes = 17;
  auto report = importer.import({shortInput, longInput}, store, token);
  for (const auto& f : report.files) { REQUIRE_FALSE(f.asset); REQUIRE(f.error); REQUIRE(f.error->code == ErrorCode::readFailure); }
  REQUIRE(entries(temp.root / "media") == 0); REQUIRE(entries(temp.root / ".import") == 0);
}
TEST_CASE("ImportCancellationDuringDecodePreservesExistingMedia", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); TestDecoder decoder; StemImporter importer(decoder); CancellationToken first;
  auto existing = importer.import({source("existing.wav", wav())}, store, first); REQUIRE(existing.files[0].asset);
  const auto bytes = readAll(store.root() / existing.files[0].asset->relativePath);
  CancellationToken token;
  auto report = importer.import({source("new.wav", wav())}, store, token,
    [&](const ImportProgress& p) { if (p.phase == ImportPhase::decoding) token.cancel(); });
  REQUIRE(report.cancelled); REQUIRE_FALSE(report.files[0].asset);
  REQUIRE(entries(temp.root / "media") == 1); REQUIRE(entries(temp.root / ".import") == 0);
  REQUIRE(readAll(store.root() / existing.files[0].asset->relativePath) == bytes);
}
TEST_CASE("ImportBoundsRejectLargeBatchesAndRollbackPaths", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); TestDecoder decoder; StemImporter importer(decoder, {1, 32}); CancellationToken token;
  auto report = importer.import({source("a.wav", wav()), source("b.wav", wav())}, store, token);
  REQUIRE(report.files.size() == 2); for (const auto& f : report.files) { REQUIRE_FALSE(f.asset); REQUIRE(f.error->code == ErrorCode::storageLimit); }
  REQUIRE_FALSE(std::filesystem::exists(temp.root / ".import"));
  AudioAsset forged; forged.id = "../external"; forged.relativePath = "media/../external/original";
  REQUIRE_FALSE(store.removeCommitted(forged));
}
TEST_CASE("ImportLateCancellationRollsBackCommittedBatch", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); TestDecoder decoder; StemImporter importer(decoder); CancellationToken token;
  auto report = importer.import({source("first.wav", wav()), source("last.wav", wav())}, store, token,
    [&](const ImportProgress& p) { if (p.phase == ImportPhase::committing && p.fileIndex == 1) token.cancel(); });
  REQUIRE(report.cancelled); for (const auto& f : report.files) { REQUIRE_FALSE(f.asset); REQUIRE(f.error); }
  REQUIRE(entries(temp.root / "media") == 0); REQUIRE(entries(temp.root / ".import") == 0);
}
TEST_CASE("MediaCommitRejectsOriginalChangedAfterStaging", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); CancellationToken token;
  auto staged = store.stage(source("changed.wav", wav()), token); REQUIRE(staged);
  { std::fstream file(staged.value().path(), std::ios::in | std::ios::out | std::ios::binary); file.seekp(44); file.put('\1'); }
  auto result = store.commit(staged.value(), 2, 48000, 8192);
  REQUIRE_FALSE(result); REQUIRE(result.error().code == ErrorCode::readFailure); REQUIRE(entries(temp.root / "media") == 0);
}
TEST_CASE("StagedCleanupFailureReportsIdentityAndCanBeRetried", "[stem-import]") {
  Temp temp; MediaStore store(temp.root); TestDecoder decoder; StemImporter importer(decoder); CancellationToken token;
  bool interruptedCopy = false, cancelDecode = false;
  SECTION("interrupted provider copy") { interruptedCopy = true; }
  SECTION("cancelled decoder") { cancelDecode = true; }
  SECTION("corrupt decoder") {}
  PreventDeletion lock; bool held = false;
  auto input = source("locked.wav", (interruptedCopy || cancelDecode) ? wav() : "garbage", interruptedCopy);
  auto report = importer.import({input}, store, token, [&](const ImportProgress& p) {
    if (!held && ((interruptedCopy && p.phase == ImportPhase::copying && p.bytesCopied > 0) || (!interruptedCopy && p.phase == ImportPhase::decoding))) {
      const auto directory = std::filesystem::directory_iterator(temp.root / ".import")->path();
      lock.hold(directory / "original"); held = true;
      if (cancelDecode) token.cancel();
    }
  });
  REQUIRE(held); REQUIRE(report.cancelled == cancelDecode); REQUIRE_FALSE(report.files[0].asset); REQUIRE(report.files[0].error);
  REQUIRE(report.files[0].unresolvedStaging);
  const auto pending = *report.files[0].unresolvedStaging;
  REQUIRE(pending.mediaId.size() == 32); REQUIRE(pending.relativeDirectory == ".import/" + pending.mediaId);
  REQUIRE(pending.error.code == ErrorCode::writeFailure);
  REQUIRE(std::filesystem::exists(store.root() / pending.relativeDirectory / "original"));
  REQUIRE_FALSE(store.retryStagedCleanup(pending));
  lock.release(); REQUIRE(store.retryStagedCleanup(pending));
  REQUIRE(entries(temp.root / ".import") == 0); REQUIRE(entries(temp.root / "media") == 0);
}

