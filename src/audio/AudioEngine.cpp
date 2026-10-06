#include "audio/AudioEngine.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace jeff::daw {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
void AudioEngine::prepare(double r, int, int) noexcept {
  rate = r > 0 ? r : 48000;
  sampleRate_.store(int(rate));
  clock.prepare(rate);
  phase = 0;
}
void AudioEngine::setAuditionNote(int note, float velocity) noexcept {
  const auto n = std::uint32_t(std::clamp(note, 0, 127) + 1);
  const auto v = std::isfinite(velocity) ? std::clamp(velocity, 0.f, 1.f) : 0.f;
  audition_.store((std::uint64_t(n) << 32) | std::bit_cast<std::uint32_t>(v),
                  std::memory_order_release);
}
void AudioEngine::handlePerformanceEvent(const PerformanceEvent &e, bool virtualPad) noexcept {
  int note = e.note + (virtualPad ? 60 : 0);
  if (e.type == PerformanceEventType::noteOn)
    setAuditionNote(note, e.value);
  else if (e.type == PerformanceEventType::noteOff) {
    auto command = audition_.load(std::memory_order_acquire);
    if (int(command >> 32) == note + 1)
      audition_.compare_exchange_strong(command, 0, std::memory_order_acq_rel);
  }
}
void AudioEngine::processBlock(float *const *channels, int count, int frames) noexcept {
  if (!channels || count <= 0 || frames <= 0)
    return;
  for (int c = 0; c < count; ++c)
    if (channels[c])
      std::fill_n(channels[c], frames, 0.f);
  auto command = audition_.load(std::memory_order_acquire);
  int note = int(command >> 32) - 1;
  float level = command ? std::bit_cast<float>(std::uint32_t(command)) * .25f : 0.f;
  double delta = command ? 6.283185307179586 * 440 * std::pow(2., (note - 69) / 12.) / rate : 0;
  auto block = clock.process(frames);
  playhead_.store(block.endBeat, std::memory_order_release);
  for (int i = 0; i < frames; ++i) {
    float sample = command ? float(std::sin(phase)) * level : 0;
    phase += delta;
    if (phase > 6.283185307179586)
      phase -= 6.283185307179586;
    for (int k = 0; k < block.clickCount; ++k)
      if (i >= block.clickOffsets[k] && i < block.clickOffsets[k] + 64)
        sample += .12f * (1 - (i - block.clickOffsets[k]) / 64.f);
    for (int c = 0; c < count; ++c)
      if (channels[c])
        channels[c][i] = sample;
  }
  stems.render(channels, count, frames, block);
  float peak = 0;
  for (int c = 0; c < count; ++c)
    if (channels[c])
      for (int i = 0; i < frames; ++i) {
        channels[c][i] = std::clamp(channels[c][i], -1.f, 1.f);
        peak = std::max(peak, std::abs(channels[c][i]));
      }
  outPeak.store(peak);
  recorder_.capture(channels, count, frames, int(rate));
}
} // namespace jeff::daw
