#include "stems/TimingMap.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace jeff::daw {

Result<void> validate(const TimingMap& map) {
  if (map.markers.size() < 2)
    return Result<void>::failure(ErrorCode::invalidTimingMap, "Timing map needs at least two markers.");
  for (std::size_t i = 0; i < map.markers.size(); ++i) {
    const auto& marker = map.markers[i];
    if (!std::isfinite(marker.sourceSeconds) || !std::isfinite(marker.destinationBeats) ||
        marker.sourceSeconds < 0.0 || marker.destinationBeats < 0.0)
      return Result<void>::failure(ErrorCode::invalidTimingMap, "Timing marker must have finite, nonnegative source seconds and destination beats.");
    if (i && (marker.sourceSeconds <= map.markers[i - 1].sourceSeconds ||
              marker.destinationBeats <= map.markers[i - 1].destinationBeats))
      return Result<void>::failure(ErrorCode::invalidTimingMap, "Timing markers must increase in source seconds and destination beats.");
  }
  return Result<void>::success();
}

double mapTime(const TimingMap& map, double sourceSeconds) {
  if (const auto result = validate(map); !result)
    throw std::invalid_argument(result.error().message);
  if (!std::isfinite(sourceSeconds))
    throw std::invalid_argument("Source position must be finite.");
  const auto after = std::upper_bound(map.markers.begin(), map.markers.end(), sourceSeconds,
      [](double seconds, const TimingMarker& marker) { return seconds < marker.sourceSeconds; });
  const std::size_t right = after == map.markers.begin() ? 1 :
      after == map.markers.end() ? map.markers.size() - 1 : static_cast<std::size_t>(after - map.markers.begin());
  const auto& a = map.markers[right - 1];
  const auto& b = map.markers[right];
  if (sourceSeconds == a.sourceSeconds) return a.destinationBeats;
  if (sourceSeconds == b.sourceSeconds) return b.destinationBeats;
  const double fraction = (sourceSeconds - a.sourceSeconds) / (b.sourceSeconds - a.sourceSeconds);
  const double destination = std::lerp(a.destinationBeats, b.destinationBeats, fraction);
  if (!std::isfinite(destination))
    throw std::overflow_error("Timing mapping arithmetic exceeded finite floating-point range.");
  return destination;
}

TimingMap preserveTimingMap(double tempoBpm) {
  return {{{0.0, 0.0}, {1.0, tempoBpm / 60.0}}};
}

} // namespace jeff::daw
