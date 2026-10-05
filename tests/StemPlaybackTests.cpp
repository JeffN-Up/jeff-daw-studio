#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "stems/PlaybackSnapshot.h"
#include "stems/StemPlayback.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>
using namespace jeff::daw;

TEST_CASE("transport keeps beat placement when the device rate changes") {
  TransportClock clock;
  clock.prepare(44100); clock.setPlaying(true);
  auto first=clock.process(44100);
  REQUIRE(first.startBeat==Catch::Approx(0));
  REQUIRE(first.endBeat==Catch::Approx(2));
  clock.prepare(48000);
  auto second=clock.process(24000);
  REQUIRE(second.startBeat==Catch::Approx(2));
  REQUIRE(second.endBeat==Catch::Approx(3));
  REQUIRE(second.endSample==Catch::Approx(72000));
}

TEST_CASE("tempo changes and paused seeks retain explicit beat positions") {
  TransportClock clock; clock.prepare(48000); clock.setPlaying(true);
  clock.process(12000);
  clock.setTempo(60);
  auto slower=clock.process(48000);
  REQUIRE(slower.startBeat==Catch::Approx(.5));
  REQUIRE(slower.endBeat==Catch::Approx(1.5));
  clock.setPlaying(false); clock.seekBeats(8.25);
  auto paused=clock.process(512);
  REQUIRE_FALSE(paused.isPlaying);
  REQUIRE(paused.startBeat==Catch::Approx(8.25));
  REQUIRE(paused.endBeat==Catch::Approx(8.25));
  REQUIRE(paused.seekGeneration==slower.seekGeneration+1);
}

TEST_CASE("playback mix preserves mono equal-power pan and stereo center balance") {
  float monoSample=.5f; const float* mono[]{&monoSample}; float l=0,r=0;
  PlaybackSnapshot::mixTrack(mono,1,1,0,l,r);
  REQUIRE(l==Catch::Approx(.5f/std::sqrt(2.0f)));
  REQUIRE(r==Catch::Approx(.5f/std::sqrt(2.0f)));
  float leftSample=.25f,rightSample=-.5f;const float* stereo[]{&leftSample,&rightSample};l=0;r=0;
  PlaybackSnapshot::mixTrack(stereo,2,1,0,l,r);
  REQUIRE(l==Catch::Approx(.25f)); REQUIRE(r==Catch::Approx(-.5f));
  l=0;r=0;PlaybackSnapshot::mixTrack(stereo,2,1,1,l,r);
  REQUIRE(l==Catch::Approx(0)); REQUIRE(r==Catch::Approx(-.5f));
}

TEST_CASE("rapid snapshot replacement and callback drain remain bounded") {
  StemPlayback playback;
  std::atomic<bool> running{true};
  std::thread callback([&]{
    std::array<float,64> left{},right{};float* out[]{left.data(),right.data()};
    TransportBlock b;b.isPlaying=true;b.tempoBpm=120;b.sampleRate=48000;b.endBeat=0;
    while(running.load(std::memory_order_acquire)) {
      b.startBeat=b.endBeat;b.endBeat+=64*120.0/(60*48000.0);
      playback.render(out,2,64,b);left.fill(0);right.fill(0);
    }
  });
  for(int i=0;i<500;++i) playback.publish(std::make_unique<PlaybackSnapshot>(120,std::vector<PlaybackTrack>{}));
  running.store(false,std::memory_order_release);callback.join();
  const auto status=playback.workerStatus();
  REQUIRE((status.empty() || status=="Prepared playback ready."));
}
