#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "stems/PlaybackSnapshot.h"
#include "stems/StemPlayback.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <mutex>
#include <memory>
#include <span>
#include <thread>
#include <unordered_map>
using namespace jeff::daw;

namespace {
class TestInput final : public InputStream {
public:
  Result<std::size_t> read(std::span<std::byte> out) override {
    if(offset_==1 || out.empty()) return Result<std::size_t>::success(0);
    out[0]=std::byte{0};offset_=1;return Result<std::size_t>::success(1);
  }
private: int offset_{};
};
struct SourcePattern {
  int channels{},rate{};Frame frames{};
  std::function<float(Frame,int)> sample;
};
class TestReader final : public AudioSampleReader {
public:
  explicit TestReader(SourcePattern p):pattern_(std::move(p)) {
    meta_.channels=pattern_.channels;meta_.sourceRate=pattern_.rate;meta_.frameCount=pattern_.frames;
  }
  const DecodedAudio& metadata() const noexcept override{return meta_;}
  Result<void> read(Frame start,int frames,std::span<float* const> out,CancellationToken& token) override {
    if(token.isCancelled())return Result<void>::failure(ErrorCode::cancelled,"cancelled");
    if(start<0||frames<0||start>meta_.frameCount-frames||out.size()!=std::size_t(meta_.channels))
      return Result<void>::failure(ErrorCode::decodeFailure,"bad fixture range");
    for(int f=0;f<frames;++f)for(int c=0;c<meta_.channels;++c)out[c][f]=pattern_.sample(start+f,c);
    return Result<void>::success();
  }
private: SourcePattern pattern_;DecodedAudio meta_;
};
class TestDecoder final : public AudioDecoder {
public:
  void add(const std::filesystem::path& p,SourcePattern pattern){patterns_[p.lexically_normal().string()]=std::move(pattern);}
  Result<DecodedAudio> inspect(const std::filesystem::path&,CancellationToken&,std::size_t) override {
    return Result<DecodedAudio>::failure(ErrorCode::decodeFailure,"unused");
  }
  Result<std::unique_ptr<AudioSampleReader>> openReader(const std::filesystem::path& p,CancellationToken&) override {
    const auto it=patterns_.find(p.lexically_normal().string());
    if(it==patterns_.end())return Result<std::unique_ptr<AudioSampleReader>>::failure(ErrorCode::decodeFailure,"missing fixture");
    return Result<std::unique_ptr<AudioSampleReader>>::success(std::make_unique<TestReader>(it->second));
  }
private: std::unordered_map<std::string,SourcePattern> patterns_;
};
struct PlaybackFixture {
  std::filesystem::path root;
  MediaStore store;
  TestDecoder decoder;
  PreparedAudio prepared;
  PlaybackFixture():root(std::filesystem::current_path()/std::filesystem::path("jds-playback-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),store(root) {
    std::filesystem::create_directories(root);
    auto first=addAsset("first.wav",2,44100,88200,[](Frame,int c){return c==0?.1f:.2f;});
    auto second=addAsset("second.wav",2,44100,88200,[](Frame f,int c){return c==0?std::min(.4f,float(f)/44100.0f*.2f):.25f;});
    StretchRenderer renderer(decoder);CancellationToken token;StretchOptions options;options.mode=TimingMode::preserve;options.renderRate=44100;
    auto result=renderer.prepareGroup({StretchMember{first},StretchMember{second}},preserveTimingMap(120),120,store,token,options);
    REQUIRE(result);prepared=std::move(result.value());
    REQUIRE(prepared.channels==4);REQUIRE(prepared.sampleRate==44100);
  }
  ~PlaybackFixture(){prepared=PreparedAudio{};std::error_code ec;std::filesystem::remove_all(root,ec);}
  AudioAsset addAsset(const std::string& name,int channels,int rate,Frame frames,std::function<float(Frame,int)> sample){
    CancellationToken token;
    auto staged=store.stage({name,1,[]{return Result<std::unique_ptr<InputStream>>::success(std::make_unique<TestInput>());}},token);
    REQUIRE(staged);auto asset=store.commit(staged.value(),channels,rate,frames);if(!asset)FAIL(asset.error().message);
    decoder.add(store.root()/asset.value().relativePath,{channels,rate,frames,std::move(sample)});return asset.value();
  }
};
PlaybackTrack selectedStereo(PreparedAudio audio,double placement=1){
  PlaybackTrack t;t.audio=std::move(audio);t.firstChannel=2;t.channels=2;t.placementBeats=placement;return t;
}
struct Captured {float left{},right{};double beat{};bool found{};};
Captured renderAt(StemPlayback& playback,TransportClock& clock,double beat,double rate){
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  float l=0,r=0;float* out[]{&l,&r};
  clock.setPlaying(false);clock.seekBeats(beat);auto paused=clock.process(1);playback.render(out,2,1,paused);
  clock.setPlaying(true);auto prime=clock.process(1);playback.render(out,2,1,prime);
  for(int attempt=0;attempt<200;++attempt){
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto block=clock.process(1);l=r=0;playback.render(out,2,1,block);
    if(std::abs(l)+std::abs(r)>1e-5f)return{l,r,block.startBeat,true};
  }
  (void)rate;return{};
}
}

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
  PlaybackFixture fixture;StemPlayback playback;
  auto path=fixture.prepared.dataPath();
  std::atomic<bool> running{true};
  std::thread callback([&]{
    std::array<float,64> left{},right{};float* out[]{left.data(),right.data()};
    TransportBlock b;b.isPlaying=true;b.tempoBpm=120;b.sampleRate=48000;b.endBeat=0;
    while(running.load(std::memory_order_acquire)) {
      b.startBeat=b.endBeat;b.endBeat+=64*120.0/(60*48000.0);
      playback.render(out,2,64,b);left.fill(0);right.fill(0);
    }
  });
  for(int i=0;i<500;++i) {
    auto track=selectedStereo(fixture.prepared);
    track.solo=true;
    playback.publish(std::make_unique<PlaybackSnapshot>(120,std::vector<PlaybackTrack>{std::move(track)}));
  }
  const auto lastGeneration=playback.snapshotGeneration()+1;
  playback.publish(std::make_unique<PlaybackSnapshot>(120,std::vector<PlaybackTrack>{}));
  for(int i=0;i<200&&playback.snapshotGeneration()<lastGeneration;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(playback.snapshotGeneration()>=lastGeneration);
  running.store(false,std::memory_order_release);callback.join();
  const auto status=playback.workerStatus();
  REQUIRE((status.empty() || status=="Prepared playback ready."));
  fixture.prepared=PreparedAudio{};
  REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("grouped prepared playback selects its channel slice at the requested beat") {
  PlaybackFixture fixture;StemPlayback playback;TransportClock clock;clock.prepare(44100);
  auto track=selectedStereo(fixture.prepared);track.solo=true;
  playback.publish(std::make_unique<PlaybackSnapshot>(120,std::vector<PlaybackTrack>{std::move(track)}));
  const auto output=renderAt(playback,clock,1.5,44100);
  INFO("worker="<<playback.workerStatus()<<" underruns="<<playback.underruns());
  REQUIRE(output.found);
  const double elapsed=(output.beat-1)*60.0/120.0;
  const float expectedLeft=float(elapsed*.2);
  const float expectedRight=.25f;
  REQUIRE(output.left==Catch::Approx(expectedLeft).margin(.0002));
  REQUIRE(output.right==Catch::Approx(expectedRight).margin(.0002));
}

TEST_CASE("prepared playback follows paused seek and a 44.1 to 48 kHz device change") {
  PlaybackFixture fixture;StemPlayback playback;TransportClock clock;clock.prepare(44100);
  playback.publish(std::make_unique<PlaybackSnapshot>(120,std::vector<PlaybackTrack>{selectedStereo(fixture.prepared)}));
  const auto first=renderAt(playback,clock,1.5,44100);
  REQUIRE(first.found);
  clock.setPlaying(false);auto pause=clock.process(1);playback.render(nullptr,0,0,pause);
  clock.prepare(48000);
  const auto changed=renderAt(playback,clock,1.5,48000);
  REQUIRE(changed.found);
  REQUIRE(changed.beat>=1.5);
  REQUIRE(changed.beat<1.51);
  const float expectedLeft=float((changed.beat-1)*60.0/120.0*.2);
  REQUIRE(changed.left==Catch::Approx(expectedLeft).margin(.0002));
  REQUIRE(changed.right==Catch::Approx(.25f).margin(.0002));
}

TEST_CASE("prepared playback applies mute solo and pan in the worker mix") {
  PlaybackFixture fixture;StemPlayback playback;TransportClock clock;clock.prepare(44100);
  auto bed=PlaybackTrack{};bed.audio=fixture.prepared;bed.firstChannel=0;bed.channels=2;bed.placementBeats=1;
  auto lead=selectedStereo(fixture.prepared);lead.solo=true;lead.pan=1;
  playback.publish(std::make_unique<PlaybackSnapshot>(120,std::vector<PlaybackTrack>{bed,lead}));
  const auto solo=renderAt(playback,clock,1.5,44100);
  REQUIRE(solo.found);REQUIRE(solo.left==Catch::Approx(0).margin(.0001));
  REQUIRE(solo.right==Catch::Approx(.25).margin(.0002));
  lead.mute=true;
  playback.publish(std::make_unique<PlaybackSnapshot>(120,std::vector<PlaybackTrack>{bed,lead}));
  const auto muted=renderAt(playback,clock,1.5,44100);
  REQUIRE_FALSE(muted.found);
}
