#pragma once
#include "stems/StretchRenderer.h"
#include <memory>
namespace jeff::daw {
struct PlaybackTrack {
  PreparedAudio audio;
  int firstChannel{}, channels{};
  double placementBeats{};
  float gain{1}, pan{};
  bool mute{}, solo{};
};
// Immutable prepared state. Construct/copy/destroy on non-audio threads.
class PlaybackSnapshot {
public:
  PlaybackSnapshot(double projectTempo, std::vector<PlaybackTrack> tracks);
  double tempoBpm() const noexcept { return tempoBpm_; }
  const std::vector<PlaybackTrack>& tracks() const noexcept { return tracks_; }
  static void mixTrack(const float* const* input, int channels, float gain, float pan,
                       float& left, float& right) noexcept;
private:
  double tempoBpm_;
  std::vector<PlaybackTrack> tracks_;
};
}
