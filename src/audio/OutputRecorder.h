#pragma once
#include "project/Types.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <thread>
namespace jeff::daw {
// One producer (audio callback), one file writer. Controls run on a non-audio thread.
class OutputRecorder {
public:
  ~OutputRecorder();
  Result<void> start(const std::filesystem::path &, int);
  Result<void> stop();
  void capture(float *const *, int, int, int) noexcept;
  bool recording() const noexcept { return enabled_.load(std::memory_order_acquire); }

private:
  static constexpr std::size_t capacity = 64;
  static constexpr int blockFrames = 1024;
  struct Block {
    std::array<float, blockFrames * 2> samples{};
    int frames{};
  };
  void writeLoop() noexcept;
  void writeHeader(std::uint32_t);
  std::array<Block, capacity> blocks_;
  std::atomic<std::uint64_t> head_{}, tail_{};
  std::atomic<bool> enabled_{}, stopping_{};
  std::atomic<unsigned> readers_{};
  std::atomic<int> error_{};
  int rate_{};
  std::uint64_t written_{};
  std::filesystem::path target_, temporary_;
  std::ofstream output_;
  std::thread writer_;
};
} // namespace jeff::daw
