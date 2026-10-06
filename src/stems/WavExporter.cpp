#include "stems/WavExporter.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
namespace jeff::daw {
Result<void> exportMixWav(const PlaybackSnapshot &snapshot, const std::filesystem::path &path,
                          CancellationToken &token) {
  constexpr int rate = 48000, block = 4096;
  auto temp = path;
  temp += std::string(".partial-") + std::to_string(std::random_device{}());
  auto fail = [&](ErrorCode code, std::string message) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Result<void>::failure(code, std::move(message));
  };
  try {
    if (token.isCancelled())
      return fail(ErrorCode::cancelled, "Export cancelled.");
    if (std::filesystem::exists(path))
      return fail(ErrorCode::writeFailure,
                  "Choose a new filename; an existing export will not be overwritten.");
    double end = 0;
    for (auto &t : snapshot.tracks())
      end = std::max(end, (t.placementBeats + t.audio.originBeats) * 60 / snapshot.tempoBpm() +
                              double(t.audio.frameCount) / t.audio.sampleRate);
    if (!std::isfinite(end) || end <= 0 || end * rate > (double(UINT32_MAX) - 36) / 6)
      return fail(ErrorCode::storageLimit, "Mix is empty or exceeds the WAV size limit.");
    const auto frames = Frame(std::ceil(end * rate));
    const auto bytes = std::uint32_t(frames * 6);
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out)
      return fail(ErrorCode::writeFailure, "Cannot create WAV export.");
    auto u16 = [&](std::uint16_t n) {
      for (int i = 0; i < 2; ++i)
        out.put(char(n >> (8 * i)));
    };
    auto u32 = [&](std::uint32_t n) {
      for (int i = 0; i < 4; ++i)
        out.put(char(n >> (8 * i)));
    };
    out.write("RIFF", 4);
    u32(bytes + 36);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(rate);
    u32(rate * 6);
    u16(6);
    u16(24);
    out.write("data", 4);
    u32(bytes);
    bool solo = std::any_of(snapshot.tracks().begin(), snapshot.tracks().end(),
                            [](auto &t) { return t.solo; });
    for (Frame start = 0; start < frames; start += block) {
      if (token.isCancelled()) {
        out.close();
        return fail(ErrorCode::cancelled, "Export cancelled.");
      }
      const int n = int(std::min<Frame>(block, frames - start));
      std::vector<float> left(n), right(n);
      for (auto &t : snapshot.tracks()) {
        if (t.mute || (solo && !t.solo))
          continue;
        auto &a = t.audio;
        const double origin = (t.placementBeats + a.originBeats) * 60 / snapshot.tempoBpm();
        const double pos = (double(start) / rate - origin) * a.sampleRate,
                     step = double(a.sampleRate) / rate;
        const double lastPos = pos + (n - 1) * step;
        if (lastPos < 0 || pos >= a.frameCount)
          continue;
        auto first = std::max<Frame>(0, Frame(std::floor(pos))),
             last = std::min<Frame>(a.frameCount - 1, Frame(std::floor(lastPos)) + 1);
        int count = int(last - first + 1);
        std::vector<std::vector<float>> planes(a.channels, std::vector<float>(count));
        std::vector<float *> pointers;
        for (auto &p : planes)
          pointers.push_back(p.data());
        auto read = a.read(first, count, pointers, token);
        if (!read) {
          out.close();
          return fail(read.error().code, read.error().message);
        }
        for (int i = 0; i < n; ++i) {
          double at = pos + i * step;
          if (at < 0 || at >= a.frameCount)
            continue;
          int lo = int(std::floor(at) - first), hi = std::min(lo + 1, count - 1);
          float fraction = float(at - std::floor(at));
          float values[64]{};
          const float *selected[64]{};
          for (int c = 0; c < t.channels; ++c) {
            values[c] =
                std::lerp(planes[t.firstChannel + c][lo], planes[t.firstChannel + c][hi], fraction);
            selected[c] = &values[c];
          }
          PlaybackSnapshot::mixTrack(selected, t.channels, t.gain, t.pan, left[i], right[i]);
        }
      }
      for (int i = 0; i < n; ++i)
        for (float sample : {left[i], right[i]}) {
          if (!std::isfinite(sample)) {
            out.close();
            return fail(ErrorCode::decodeFailure, "Nonfinite mix sample.");
          }
          auto pcm = std::int32_t(std::lround(std::clamp(sample, -1.0f, 1.0f) * 8388607));
          for (int byte = 0; byte < 3; ++byte)
            out.put(char(std::uint32_t(pcm) >> (8 * byte)));
        }
      if (!out) {
        out.close();
        return fail(ErrorCode::writeFailure, "WAV export write failed.");
      }
    }
    out.close();
    if (!out)
      return fail(ErrorCode::writeFailure, "Cannot finish WAV export.");
    if (token.isCancelled())
      return fail(ErrorCode::cancelled, "Export cancelled.");
    // A hard-link publishes atomically without replacing a file created during the export.
    std::filesystem::create_hard_link(temp, path);
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Result<void>::success();
  } catch (const std::exception &e) {
    return fail(ErrorCode::writeFailure, e.what());
  }
}
} // namespace jeff::daw
