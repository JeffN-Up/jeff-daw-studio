#include "stems/WavExporter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace jeff::daw {
namespace {
constexpr int outputChannels = 2;
constexpr int bytesPerSample = WavExporter::bitsPerSample / 8;
constexpr int blockFrames = 4096;

void append16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(std::byte(value & 0xff));
  out.push_back(std::byte((value >> 8) & 0xff));
}
void append32(std::vector<std::byte>& out, std::uint32_t value) {
  for(int shift = 0; shift < 32; shift += 8)
    out.push_back(std::byte((value >> shift) & 0xff));
}
void appendText(std::vector<std::byte>& out, const char* text, int count) {
  for(int i = 0; i < count; ++i) out.push_back(std::byte(text[i]));
}

Result<Frame> exportFrames(const PlaybackSnapshot& snapshot, ExportRange range) {
  if(!std::isfinite(range.startBeat) || !std::isfinite(range.endBeat) ||
     range.startBeat < 0 || range.endBeat <= range.startBeat)
    return Result<Frame>::failure(ErrorCode::invalidCommand, "Export range must have finite increasing beats.");
  const auto exact = (range.endBeat - range.startBeat) * 60.0 /
                     snapshot.tempoBpm() * WavExporter::sampleRate;
  if(!std::isfinite(exact) || exact <= 0)
    return Result<Frame>::failure(ErrorCode::invalidCommand, "Export range is invalid.");
  const auto frames = Frame(std::ceil(exact));
  constexpr auto maxData = std::uint64_t(std::numeric_limits<std::uint32_t>::max()) - 36;
  if(std::uint64_t(frames) > maxData / (outputChannels * bytesPerSample))
    return Result<Frame>::failure(ErrorCode::storageLimit, "Export is too large for a standard WAV file.");
  return Result<Frame>::success(frames);
}

std::vector<std::byte> wavHeader(Frame frames) {
  const auto dataBytes = std::uint32_t(frames * outputChannels * bytesPerSample);
  std::vector<std::byte> out;
  out.reserve(44);
  appendText(out, "RIFF", 4); append32(out, 36 + dataBytes); appendText(out, "WAVE", 4);
  appendText(out, "fmt ", 4); append32(out, 16); append16(out, 1); append16(out, outputChannels);
  append32(out, WavExporter::sampleRate);
  append32(out, WavExporter::sampleRate * outputChannels * bytesPerSample);
  append16(out, outputChannels * bytesPerSample); append16(out, WavExporter::bitsPerSample);
  appendText(out, "data", 4); append32(out, dataBytes);
  return out;
}

Result<void> addTrack(const PlaybackTrack& track, const PlaybackSnapshot& snapshot,
                      ExportRange range, Frame outputStart, int count,
                      std::vector<float>& left, std::vector<float>& right,
                      CancellationToken& token) {
  const double beatStep = snapshot.tempoBpm() / (60.0 * WavExporter::sampleRate);
  const double trackOrigin = track.placementBeats + track.audio.originBeats;
  const double sourceStep = double(track.audio.sampleRate) / WavExporter::sampleRate;
  const double firstBeat = range.startBeat + double(outputStart) * beatStep;
  const double firstSource = (firstBeat - trackOrigin) * 60.0 /
                             snapshot.tempoBpm() * track.audio.sampleRate;
  const double lastSource = firstSource + double(count - 1) * sourceStep;
  const Frame readStart = std::max<Frame>(0, Frame(std::floor(firstSource)));
  const Frame readEnd = std::min<Frame>(track.audio.frameCount,
      Frame(std::floor(lastSource)) + 2);
  if(readEnd <= readStart || lastSource < 0 || firstSource >= track.audio.frameCount)
    return Result<void>::success();

  const auto readCount = int(readEnd - readStart);
  std::vector<std::vector<float>> planes(std::size_t(track.audio.channels),
                                         std::vector<float>(std::size_t(readCount)));
  std::vector<float*> planePointers;
  planePointers.reserve(planes.size());
  for(auto& plane : planes) planePointers.push_back(plane.data());
  auto read = track.audio.read(readStart, readCount, planePointers, token);
  if(!read) return read;

  std::array<float, 64> selected{};
  std::array<const float*, 64> selectedPointers{};
  for(int channel = 0; channel < track.channels; ++channel)
    selectedPointers[std::size_t(channel)] = &selected[std::size_t(channel)];
  for(int frame = 0; frame < count; ++frame) {
    const double source = firstSource + double(frame) * sourceStep;
    if(source < 0 || source >= track.audio.frameCount) continue;
    const Frame lower = Frame(std::floor(source));
    const Frame upper = std::min<Frame>(lower + 1, track.audio.frameCount - 1);
    const float fraction = float(source - double(lower));
    for(int channel = 0; channel < track.channels; ++channel) {
      const auto preparedChannel = std::size_t(track.firstChannel + channel);
      const auto a = planes[preparedChannel][std::size_t(lower - readStart)];
      const auto b = planes[preparedChannel][std::size_t(upper - readStart)];
      selected[std::size_t(channel)] = a + (b - a) * fraction;
    }
    PlaybackSnapshot::mixTrack(selectedPointers.data(), track.channels, track.gain,
                               track.pan, left[std::size_t(frame)], right[std::size_t(frame)]);
  }
  return Result<void>::success();
}

void appendPcm24(std::vector<std::byte>& bytes, float sample, bool& clipped) {
  if(sample < -1.0f || sample > 1.0f) clipped = true;
  const auto bounded = std::clamp(sample, -1.0f, 1.0f);
  const auto value = std::int32_t(std::lrint(bounded * 8388607.0f));
  bytes.push_back(std::byte(value & 0xff));
  bytes.push_back(std::byte((value >> 8) & 0xff));
  bytes.push_back(std::byte((value >> 16) & 0xff));
}

Result<ExportedFile> renderFile(const PlaybackSnapshot& snapshot, ExportRange range,
                                Frame totalFrames, std::span<const std::size_t> indices,
                                const std::string& preferredName, OutputTarget& target,
                                CancellationToken& token, bool& clipped,
                                const ExportProgressCallback& progress, double progressBase,
                                double progressScale, OverwritePolicy overwrite) {
  auto opened = target.openStaged(preferredName, overwrite);
  if(!opened) return Result<ExportedFile>::failure(opened.error().code, opened.error().message);
  auto staged = std::move(opened.value());
  if(!staged.stream)
    return Result<ExportedFile>::failure(ErrorCode::writeFailure, "Output target returned no staged stream.");
  auto fail = [&](ErrorCode code, std::string message) {
    staged.stream->discard();
    return Result<ExportedFile>::failure(code, std::move(message));
  };
  auto header = wavHeader(totalFrames);
  auto written = staged.stream->write(header);
  if(!written) return fail(written.error().code, written.error().message);

  std::vector<float> left(blockFrames), right(blockFrames);
  std::vector<std::byte> pcm;
  pcm.reserve(std::size_t(blockFrames * outputChannels * bytesPerSample));
  for(Frame start = 0; start < totalFrames; start += blockFrames) {
    if(token.isCancelled()) return fail(ErrorCode::cancelled, "WAV export cancelled.");
    const int count = int(std::min<Frame>(blockFrames, totalFrames - start));
    std::fill_n(left.begin(), count, 0.0f);
    std::fill_n(right.begin(), count, 0.0f);
    for(const auto index : indices) {
      auto mixed = addTrack(snapshot.tracks()[index], snapshot, range, start, count,
                            left, right, token);
      if(!mixed) return fail(mixed.error().code, mixed.error().message);
    }
    pcm.clear();
    for(int frame = 0; frame < count; ++frame) {
      appendPcm24(pcm, left[std::size_t(frame)], clipped);
      appendPcm24(pcm, right[std::size_t(frame)], clipped);
    }
    written = staged.stream->write(pcm);
    if(!written) return fail(written.error().code, written.error().message);
    if(progress) progress(progressBase + progressScale * double(start + count) / double(totalFrames));
  }
  auto committed = staged.stream->commit();
  if(!committed) return fail(committed.error().code, committed.error().message);
  return Result<ExportedFile>::success({std::move(staged.name), totalFrames});
}
} // namespace

Result<ExportReport> WavExporter::exportMix(const PlaybackSnapshot& snapshot, ExportRange range,
                                            OutputTarget& target, CancellationToken& token,
                                            ExportProgressCallback progress, OverwritePolicy overwrite) const {
  auto frames = exportFrames(snapshot, range);
  if(!frames) return Result<ExportReport>::failure(frames.error().code, frames.error().message);
  if(token.isCancelled()) return Result<ExportReport>::failure(ErrorCode::cancelled, "WAV export cancelled.");
  const bool hasSolo = std::any_of(snapshot.tracks().begin(), snapshot.tracks().end(),
                                   [](const auto& track) { return track.solo && !track.mute; });
  std::vector<std::size_t> indices;
  for(std::size_t i = 0; i < snapshot.tracks().size(); ++i) {
    const auto& track = snapshot.tracks()[i];
    if(!track.mute && (!hasSolo || track.solo)) indices.push_back(i);
  }
  ExportReport report;
  auto file = renderFile(snapshot, range, frames.value(), indices, "mix.wav", target, token,
                         report.clipped, progress, 0, 1, overwrite);
  if(!file) return Result<ExportReport>::failure(file.error().code, file.error().message);
  report.files.push_back(std::move(file.value()));
  if(progress) progress(1);
  return Result<ExportReport>::success(std::move(report));
}

Result<ExportReport> WavExporter::exportTracks(const PlaybackSnapshot& snapshot, ExportRange range,
                                               OutputTarget& target, CancellationToken& token,
                                               ExportProgressCallback progress, OverwritePolicy overwrite) const {
  auto frames = exportFrames(snapshot, range);
  if(!frames) return Result<ExportReport>::failure(frames.error().code, frames.error().message);
  if(token.isCancelled()) return Result<ExportReport>::failure(ErrorCode::cancelled, "WAV export cancelled.");
  ExportReport report;
  const auto count = snapshot.tracks().size();
  for(std::size_t i = 0; i < count; ++i) {
    std::array<std::size_t, 1> selected{i};
    std::span<const std::size_t> indices = snapshot.tracks()[i].mute
      ? std::span<const std::size_t>{}
      : std::span<const std::size_t>{selected};
    const auto number = std::to_string(i + 1);
    const auto name = "track-" + std::string(number.size() < 2 ? "0" : "") + number + ".wav";
    auto file = renderFile(snapshot, range, frames.value(), indices, name, target, token,
                           report.clipped, progress, count ? double(i) / count : 0,
                           count ? 1.0 / count : 1, overwrite);
    if(!file) return Result<ExportReport>::failure(file.error().code, file.error().message);
    report.files.push_back(std::move(file.value()));
  }
  if(progress) progress(1);
  return Result<ExportReport>::success(std::move(report));
}

} // namespace jeff::daw
