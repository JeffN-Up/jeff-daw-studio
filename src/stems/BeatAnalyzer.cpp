#include "stems/BeatAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace jeff::daw {
namespace {
struct Peak { std::size_t bin; double strength; };
Result<BeatEstimate> unresolved(std::string reason) {
  BeatEstimate estimate;
  estimate.correctionReason = std::move(reason);
  return Result<BeatEstimate>::success(std::move(estimate));
}
}
BeatAnalyzer::BeatAnalyzer(AudioDecoder& decoder, MediaStore& store, BeatAnalysisLimits limits)
    : decoder_(decoder), store_(store), limits_(limits) {}

Result<BeatEstimate> BeatAnalyzer::analyze(const AudioAsset& asset, CancellationToken& token) {
  return analyze(asset, 0, asset.sourceRate > 0 ? double(asset.frameCount)/asset.sourceRate : 0, token);
}

Result<BeatEstimate> BeatAnalyzer::analyze(const AudioAsset& asset, double start, double end, CancellationToken& token) {
  using R = Result<BeatEstimate>;
  try {
    if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "Beat analysis cancelled.");
    // Hard caps bound caller-controlled memory and autocorrelation work.
    if (!std::isfinite(limits_.maxSeconds) || limits_.maxSeconds < 2 || limits_.maxSeconds > 180 ||
        limits_.envelopeRate < 100 || limits_.envelopeRate > 400 || limits_.blockFrames < 1 ||
        limits_.blockFrames > 16384 || limits_.maxChannels < 1 || limits_.maxChannels > 64)
      return R::failure(ErrorCode::invalidCommand, "Invalid bounded beat-analysis limits.");
    if (asset.channels < 1 || asset.channels > limits_.maxChannels || asset.sourceRate < 8000 ||
        asset.sourceRate > 192000 || asset.frameCount <= 0 || asset.frameCount>Frame(asset.sourceRate)*8*60*60 || !std::isfinite(start) || !std::isfinite(end) ||
        start < 0 || end <= start || end > double(asset.frameCount)/asset.sourceRate)
      return R::failure(ErrorCode::decodeFailure, "Invalid beat-analysis metadata or trim window.");
    // Reuse Project's safe media path validation before opening any reader.
    Project pathCheck; pathCheck.projectId="analysis"; pathCheck.revisionId="analysis";
    pathCheck.assets.push_back(asset);
    if (auto valid=validate(pathCheck); !valid) return R::failure(valid.error().code,valid.error().message);
    auto opened = decoder_.openReader(store_.root()/asset.relativePath, token);
    if (!opened) return R::failure(opened.error().code, opened.error().message);
    if (!opened.value()) return R::failure(ErrorCode::decodeFailure, "Decoder returned no analysis reader.");
    auto& reader = *opened.value();
    const auto& metadata = reader.metadata();
    if (metadata.channels != asset.channels || metadata.sourceRate != asset.sourceRate || metadata.frameCount != asset.frameCount)
      return R::failure(ErrorCode::decodeFailure, "Analysis media metadata differs from the project asset.");
    const Frame first = Frame(std::ceil(start*asset.sourceRate));
    const Frame last = std::min(asset.frameCount, Frame(std::ceil(std::min(end,start+limits_.maxSeconds)*asset.sourceRate)));
    if (last <= first) return R::failure(ErrorCode::decodeFailure, "Analysis trim has no source frames.");
    const double windowStart = double(first)/asset.sourceRate;
    const auto binCount = std::size_t(std::ceil(double(last-first)*limits_.envelopeRate/asset.sourceRate));
    std::vector<double> energy(binCount,0), onset(binCount,0);
    std::vector<int> counts(binCount,0);
    std::vector<std::vector<float>> block(asset.channels,std::vector<float>(limits_.blockFrames));
    std::vector<float*> planes;
    for (auto& channel : block) planes.push_back(channel.data());
    for (Frame frame=first; frame<last;) {
      if (token.isCancelled()) return R::failure(ErrorCode::cancelled,"Beat analysis cancelled.");
      const int count=int(std::min<Frame>(limits_.blockFrames,last-frame));
      auto read=reader.read(frame,count,planes,token);
      if (!read) return R::failure(read.error().code,read.error().message);
      if (token.isCancelled()) return R::failure(ErrorCode::cancelled,"Beat analysis cancelled.");
      for (int f=0;f<count;++f) {
        double magnitude=0;
        for (const auto& channel : block) {
          if (!std::isfinite(channel[f])) return R::failure(ErrorCode::decodeFailure,"Analysis source contains nonfinite samples.");
          magnitude=std::max(magnitude,std::abs(double(channel[f])));
        }
        const auto bin=std::min(binCount-1,std::size_t((frame-first+f)*limits_.envelopeRate/asset.sourceRate));
        energy[bin]+=magnitude; ++counts[bin];
      }
      frame+=count;
    }
    double maximum=0,total=0;
    for (std::size_t i=0;i<binCount;++i) {
      energy[i]/=std::max(1,counts[i]);
      onset[i]=std::max(0.0,energy[i]-(i ? energy[i-1] : 0));
      maximum=std::max(maximum,onset[i]); total+=onset[i]*onset[i];
    }
    if (maximum < 1e-7 || total < 1e-12) return unresolved("No reliable rhythmic onsets; enter source BPM and downbeat.");
    std::vector<Peak> peaks;
    const auto separation=std::size_t(limits_.envelopeRate*.08);
    for (std::size_t i=0;i<binCount;++i) {
      if (onset[i] < maximum*.12 || (i && onset[i]<onset[i-1]) || (i+1<binCount && onset[i]<=onset[i+1])) continue;
      if (!peaks.empty() && i-peaks.back().bin<separation) {
        if (onset[i]>peaks.back().strength) peaks.back()={i,onset[i]};
      } else peaks.push_back({i,onset[i]});
    }
    if (peaks.size()<6) return unresolved("Too few consistent beats; enter source BPM and downbeat.");
    // Compare complete short onset events, not one quantized envelope bin:
    // a strong beat split across bins must not become a later bar's downbeat.
    maximum=0;
    for(auto& peak:peaks) {
      peak.strength=0;
      const auto begin=peak.bin ? peak.bin-1 : 0;
      const auto finish=std::min(binCount,peak.bin+std::size_t(limits_.envelopeRate*.025)+1);
      for(auto i=begin;i<finish;++i) peak.strength+=energy[i];
      maximum=std::max(maximum,peak.strength);
    }
    const int minLag=int(std::ceil(limits_.envelopeRate*60.0/240));
    const int maxLag=int(std::floor(limits_.envelopeRate*60.0/40));
    std::vector<double> scores(maxLag+1,0);
    for (int lag=minLag;lag<=maxLag;++lag) {
      if (token.isCancelled()) return R::failure(ErrorCode::cancelled,"Beat analysis cancelled.");
      double dot=0,left=0,right=0;
      for (std::size_t i=std::size_t(lag);i<binCount;++i) {
        dot+=onset[i]*onset[i-lag]; left+=onset[i]*onset[i]; right+=onset[i-lag]*onset[i-lag];
      }
      const double correlation=left>0 && right>0 ? dot/std::sqrt(left*right) : 0;
      int matched=0;
      const double phase=double(peaks.front().bin);
      for (const auto& peak : peaks) {
        const double distance=std::abs(std::remainder(double(peak.bin)-phase,double(lag)));
        if (distance<=std::max(2.0,lag*.06)) ++matched;
      }
      const double expected=1+double(peaks.back().bin-peaks.front().bin)/lag;
      const double coverage=std::min(1.0,matched/expected)*matched/peaks.size();
      scores[lag]=.45*correlation+.55*coverage;
    }
    BeatEstimate estimate;
    for (int lag=minLag;lag<=maxLag;++lag) {
      if (scores[lag]<.2 || (lag>minLag && scores[lag]<scores[lag-1]) || (lag<maxLag && scores[lag]<=scores[lag+1])) continue;
      estimate.candidates.push_back({60.0*limits_.envelopeRate/lag,scores[lag]});
    }
    std::sort(estimate.candidates.begin(),estimate.candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    if (estimate.candidates.empty()) return unresolved("Conflicting rhythm; enter source BPM and downbeat.");
    if (estimate.candidates.size()>8) estimate.candidates.resize(8);
    estimate.tempoBpm=estimate.candidates.front().bpm;
    // Refine quantized autocorrelation with independently observed intervals.
    std::vector<double> intervals;
    for (std::size_t i=1;i<peaks.size();++i) intervals.push_back(double(peaks[i].bin-peaks[i-1].bin)/limits_.envelopeRate);
    std::sort(intervals.begin(),intervals.end());
    const double cadence=60/intervals[intervals.size()/2];
    if (std::abs(cadence/estimate.tempoBpm-1)<.03) estimate.tempoBpm=cadence;
    const auto downbeat=std::find_if(peaks.begin(),peaks.end(),[&](const auto& peak){return peak.strength>=maximum*.8;});
    estimate.candidateDownbeatSeconds=windowStart+double(downbeat->bin)/limits_.envelopeRate;
    for (const auto& peak : peaks) estimate.beatTimesSeconds.push_back(windowStart+double(peak.bin)/limits_.envelopeRate);
    estimate.confidence=std::clamp(estimate.candidates.front().score,0.0,1.0);
    const double weakest=std::min_element(peaks.begin(),peaks.end(),[](const auto& a,const auto& b){return a.strength<b.strength;})->strength;
    const bool flatAccents=maximum/std::max(weakest,1e-12)<1.8;
    bool conflicting=false;
    for (std::size_t i=1;i<estimate.candidates.size();++i) {
      const double ratio=estimate.candidates[i].bpm/estimate.tempoBpm;
      if (estimate.candidates[i].score>=estimate.confidence*.92 && (ratio<.94 || ratio>1.06)) conflicting=true;
    }
    estimate.needsCorrection=flatAccents || conflicting || estimate.confidence<.65;
    if (estimate.needsCorrection) estimate.correctionReason="Ambiguous tempo or downbeat (including half/double tempo); enter source BPM and downbeat.";
    return R::success(std::move(estimate));
  } catch (const std::exception& e) {
    return R::failure(ErrorCode::decodeFailure,"Beat analysis failed: "+std::string(e.what()));
  }
}
} // namespace jeff::daw
