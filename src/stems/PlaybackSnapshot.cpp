#include "stems/PlaybackSnapshot.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace jeff::daw {
PlaybackSnapshot::PlaybackSnapshot(double tempo, std::vector<PlaybackTrack> tracks)
  : tempoBpm_(tempo), tracks_(std::move(tracks)) {
  if(!std::isfinite(tempoBpm_) || tempoBpm_<20 || tempoBpm_>300 || tracks_.size()>64)
    throw std::invalid_argument("Invalid playback snapshot tempo or track count.");
  for(const auto& t:tracks_)
    if(t.channels<1 || t.channels>64 || t.firstChannel<0 || t.firstChannel+t.channels>t.audio.channels ||
       t.audio.sampleRate<1 || !std::isfinite(t.placementBeats) || !std::isfinite(t.gain) ||
       !std::isfinite(t.pan) || t.pan < -1 || t.pan > 1)
      throw std::invalid_argument("Invalid prepared playback track.");
}
void PlaybackSnapshot::mixTrack(const float* const* in,int n,float gain,float pan,float& l,float& r) noexcept {
  if(!in || n<1) return;
  float left=0,right=0;
  if(n==1) {
    const float angle=(pan+1.0f)*0.7853981633974483f;
    left=in[0][0]*std::cos(angle); right=in[0][0]*std::sin(angle);
  } else {
    float a=0,b=0; int na=0,nb=0;
    for(int c=0;c<n;++c) if(in[c]) {
      if((c&1)==0) {a+=in[c][0];++na;} else {b+=in[c][0];++nb;}
    }
    left=na?a/na:0; right=nb?b/nb:left;
    // Stereo uses constant unity gain at center; wider layouts fold odd/even channels by average.
    if(pan>0) left*=1-pan; else if(pan<0) right*=1+pan;
  }
  l+=left*gain; r+=right*gain;
}
}
