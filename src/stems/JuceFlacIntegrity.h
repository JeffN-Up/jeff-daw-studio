#pragma once
#include "project/Types.h"
#include <functional>
namespace juce { class InputStream; }
namespace jeff::daw {
struct VerifiedFlac {
  int channels = 0, sampleRate = 0;
  Frame frames = 0;
};
// Sequential worker pass over the same owned stream used for sample decoding.
// Discards decoded blocks; uses codec CRC/count/MD5, never audio amplitude.
Result<VerifiedFlac> verifyFlac(juce::InputStream&, CancellationToken&,
                               const std::function<bool()>& streamFailed);
}
