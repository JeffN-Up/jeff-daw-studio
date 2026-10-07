#pragma once
#include "stems/StemImporter.h"

namespace jeff::daw {
struct TempoCandidate { double bpm = 0, score = 0; };
struct BeatEstimate {
  double tempoBpm = 0;
  std::vector<double> beatTimesSeconds;
  double candidateDownbeatSeconds = 0;
  double confidence = 0;
  std::vector<TempoCandidate> candidates;
  bool needsCorrection = true;
  std::string correctionReason;
};
struct BeatAnalysisLimits {
  double maxSeconds = 180;
  int envelopeRate = 200;
  int blockFrames = 4096;
  int maxChannels = 64;
};
// Background-only, bounded local signal analysis. A candidate downbeat is a
// suggestion, never evidence of bar/phrase compatibility.
class BeatAnalyzer {
public:
  BeatAnalyzer(AudioDecoder& decoder, MediaStore& store, BeatAnalysisLimits limits = {});
  Result<BeatEstimate> analyze(const AudioAsset&, CancellationToken&);
  Result<BeatEstimate> analyze(const AudioAsset&, double startSeconds, double endSeconds, CancellationToken&);
private:
  AudioDecoder& decoder_;
  MediaStore& store_;
  BeatAnalysisLimits limits_;
};
} // namespace jeff::daw
