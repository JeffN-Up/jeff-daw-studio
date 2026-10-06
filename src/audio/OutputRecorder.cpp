#include "audio/OutputRecorder.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
namespace jeff::daw {
OutputRecorder::~OutputRecorder() {
  if (writer_.joinable())
    stop();
}
void OutputRecorder::writeHeader(std::uint32_t bytes) {
  auto u16 = [&](std::uint16_t n) {
    for (int i = 0; i < 2; ++i)
      output_.put(char(n >> (i * 8)));
  };
  auto u32 = [&](std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
      output_.put(char(n >> (i * 8)));
  };
  output_.seekp(0);
  output_.write("RIFF", 4);
  u32(bytes + 36);
  output_.write("WAVEfmt ", 8);
  u32(16);
  u16(1);
  u16(2);
  u32(rate_);
  u32(rate_ * 6);
  u16(6);
  u16(24);
  output_.write("data", 4);
  u32(bytes);
}
Result<void> OutputRecorder::start(const std::filesystem::path &path, int rate) {
  if (writer_.joinable())
    return Result<void>::failure(ErrorCode::invalidCommand, "Finish the current recording first.");
  if (rate < 8000 || rate > 192000)
    return Result<void>::failure(ErrorCode::invalidCommand, "Unsupported recording sample rate.");
  try {
    if (std::filesystem::exists(path))
      return Result<void>::failure(ErrorCode::writeFailure, "Recording filename already exists.");
    target_ = path;
    temporary_ = path;
    temporary_ += std::string(".partial-") + std::to_string(std::random_device{}());
    rate_ = rate;
    written_ = 0;
    head_ = 0;
    tail_ = 0;
    error_ = 0;
    stopping_ = false;
    output_.clear();
    output_.open(temporary_, std::ios::binary | std::ios::trunc);
    writeHeader(0);
    if (!output_) {
      output_.close();
      std::error_code ec;
      std::filesystem::remove(temporary_, ec);
      return Result<void>::failure(ErrorCode::writeFailure, "Cannot create recording file.");
    }
    writer_ = std::thread([this] { writeLoop(); });
    enabled_.store(true, std::memory_order_release);
    return Result<void>::success();
  } catch (const std::exception &e) {
    output_.close();
    std::error_code ec;
    std::filesystem::remove(temporary_, ec);
    return Result<void>::failure(ErrorCode::writeFailure, e.what());
  }
}
void OutputRecorder::capture(float *const *channels, int count, int frames, int rate) noexcept {
  readers_.fetch_add(1, std::memory_order_acq_rel);
  if (!enabled_.load(std::memory_order_acquire) || !channels || count < 1 || frames <= 0) {
    readers_.fetch_sub(1, std::memory_order_release);
    return;
  }
  if (rate != rate_) {
    error_ = 2;
    enabled_ = false;
    readers_.fetch_sub(1, std::memory_order_release);
    return;
  }
  for (int start = 0; start < frames; start += blockFrames) {
    auto head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) >= capacity) {
      error_ = 1;
      enabled_ = false;
      break;
    }
    auto &b = blocks_[head % capacity];
    b.frames = std::min(blockFrames, frames - start);
    for (int i = 0; i < b.frames; ++i) {
      b.samples[i * 2] = channels[0] ? channels[0][start + i] : 0;
      b.samples[i * 2 + 1] = count > 1 && channels[1] ? channels[1][start + i] : b.samples[i * 2];
    }
    head_.store(head + 1, std::memory_order_release);
  }
  readers_.fetch_sub(1, std::memory_order_release);
}
void OutputRecorder::writeLoop() noexcept {
  while (!stopping_.load(std::memory_order_acquire) ||
         tail_.load(std::memory_order_relaxed) < head_.load(std::memory_order_acquire)) {
    auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    auto &b = blocks_[tail % capacity];
    if (written_ + std::uint64_t(b.frames) * 6 > UINT32_MAX - 36) {
      error_ = 3;
      enabled_ = false;
    }
    if (error_.load() == 0) {
      for (int i = 0; i < b.frames * 2; ++i) {
        float sample = b.samples[i];
        if (!std::isfinite(sample)) {
          error_ = 4;
          enabled_ = false;
          break;
        }
        auto pcm = std::int32_t(std::lround(std::clamp(sample, -1.f, 1.f) * 8388607));
        for (int byte = 0; byte < 3; ++byte)
          output_.put(char(std::uint32_t(pcm) >> (byte * 8)));
      }
      written_ += std::uint64_t(b.frames) * 6;
      if (!output_) {
        error_ = 5;
        enabled_ = false;
      }
    }
    tail_.store(tail + 1, std::memory_order_release);
  }
}
Result<void> OutputRecorder::stop() {
  if (!writer_.joinable())
    return Result<void>::failure(ErrorCode::invalidCommand, "No recording is open.");
  enabled_.store(false, std::memory_order_release);
  while (readers_.load(std::memory_order_acquire))
    std::this_thread::yield();
  stopping_.store(true, std::memory_order_release);
  writer_.join();
  auto discard = [&](std::string message) {
    output_.close();
    std::error_code ec;
    std::filesystem::remove(temporary_, ec);
    return Result<void>::failure(ErrorCode::writeFailure, std::move(message));
  };
  if (error_.load() != 0)
    return discard(
        "Recording failed (buffer, device rate or file error); no partial take was published.");
  if (written_ == 0)
    return discard("No audio was captured. Check the output device before recording.");
  writeHeader(std::uint32_t(written_));
  output_.close();
  if (!output_)
    return discard("Cannot finish recorded WAV.");
  try {
    std::filesystem::create_hard_link(temporary_, target_);
    std::error_code ec;
    std::filesystem::remove(temporary_, ec);
    return Result<void>::success();
  } catch (const std::exception &e) {
    return discard(e.what());
  }
}
} // namespace jeff::daw
