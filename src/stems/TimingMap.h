#pragma once

#include <vector>
#include "project/Types.h"

namespace jeff::daw {

struct TimingMarker {
  double sourceSeconds = 0.0;
  double destinationBeats = 0.0;
};

struct TimingMap {
  std::vector<TimingMarker> markers;
};

Result<void> validate(const TimingMap& map);
// Checked, off-audio-thread mapping. The map must pass validate() and the
// query must be finite. Uses end segments for extrapolation; exact markers
// return their stored beat value. Throws invalid_argument for invalid input
// and overflow_error when mapping arithmetic cannot produce a finite double.
// Prepare/validate playback positions before publishing an audio snapshot;
// do not call this checked, potentially throwing boundary in the callback.
double mapTime(const TimingMap& map, double sourceSeconds);
TimingMap preserveTimingMap(double tempoBpm);

} // namespace jeff::daw
