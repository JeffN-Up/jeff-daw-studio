#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "stems/StretchRenderer.h"
#ifdef JDS_TEST_JUCE_IMPORT
#include "stems/JuceStemDecoder.h"
#endif
#include <fstream>
#include <atomic>
#include <chrono>
#include <cmath>
#include <array>
#include <algorithm>
#include <cstring>
using namespace jeff::daw;
namespace {
  struct Temp {
    std::filesystem::path root;
    Temp() {
      static std::atomic<int> n{
        0
      };
      root = std::filesystem::current_path() / ("jds-stretch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(n++));
      std::filesystem::create_directories(root);
    }
    ~Temp() {
      std::error_code ec;
      std::filesystem::remove_all(root, ec);
    }
  };
  void u16(std::ostream& out, unsigned v) {
    out.put(char(v));
    out.put(char(v >> 8));
  }
  void u32(std::ostream& out, unsigned v) {
    u16(out,v);
    u16(out,v >> 16);
  }
  unsigned get16(const unsigned char* b) {
    return b[0] | unsigned(b[1]) << 8;
  }
  unsigned get32(const unsigned char* b) {
    return get16(b) | get16(b+2) << 16;
  }
  void writeWav(const std::filesystem::path& p, int channels, int rate, Frame frames,
  const std::function<float(Frame,int)>& sample) {
    std::ofstream out(p,std::ios::binary);
    const auto bytes = unsigned(frames * channels * 2);
    out << "RIFF";
    u32(out,36+bytes);
    out << "WAVEfmt ";
    u32(out,16);
    u16(out,1);
    u16(out,channels);
    u32(out,rate);
    u32(out,rate*channels*2);
    u16(out,channels*2);
    u16(out,16);
    out << "data";
    u32(out,bytes);
    for (Frame f=0; f<frames; ++f) for (int c=0; c<channels; ++c) u16(out,unsigned(std::int16_t(std::clamp(sample(f,c),-1.0f,1.0f)*32767)));
    REQUIRE(out.good());
  }
  class FileStream final : public InputStream {
    public:
    explicit FileStream(const std::filesystem::path& p) : in_(p,std::ios::binary) {
    }
    Result<std::size_t> read(std::span<std::byte> out) override {
      in_.read(reinterpret_cast<char*>(out.data()),out.size());
      if(in_.bad()) return Result<std::size_t>::failure(ErrorCode::readFailure,"Fixture read failed.");
      return Result<std::size_t>::success(std::size_t(in_.gcount()));
    }
    private: std::ifstream in_;
  };
  AudioAsset install(MediaStore& store, const std::filesystem::path& path, int channels, int rate, Frame frames) {
    CancellationToken token;
    InputStreamFactory f{
      path.filename().string(),std::filesystem::file_size(path),[path] {
        return Result<std::unique_ptr<InputStream>>::success(std::make_unique<FileStream>(path));
      }
    };
    auto staged=store.stage(f,token);
    REQUIRE(staged);
    auto asset=store.commit(staged.value(),channels,rate,frames);
    REQUIRE(asset);
    return asset.value();
  }
  #ifndef JDS_TEST_JUCE_IMPORT
  // Test-only disk PCM WAV adapter. No full-file samples or production format claim.
  class WavReader final : public AudioSampleReader {
    public:
    explicit WavReader(const std::filesystem::path& p) : in_(p,std::ios::binary) {
      std::array<unsigned char,44> b{
      };
      in_.read(reinterpret_cast<char*>(b.data()),b.size());
      if(in_.gcount()!=44 || std::memcmp(b.data(),"RIFF",4) || std::memcmp(b.data()+8,"WAVE",4) || get16(b.data()+20)!=1 || get16(b.data()+34)!=16) return;
      metadata_.channels=int(get16(b.data()+22));
      metadata_.sourceRate=int(get32(b.data()+24));
      if(metadata_.channels>0) metadata_.frameCount=get32(b.data()+40)/(metadata_.channels*2);
    }
    const DecodedAudio& metadata() const noexcept override {
      return metadata_;
    }
    Result<void> read(Frame start,int count,std::span<float* const> out,CancellationToken& token) override {
      if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled,"Cancelled fixture read.");
      if(start<0 || count<0 || start>metadata_.frameCount-count || out.size()!=std::size_t(metadata_.channels)) return Result<void>::failure(ErrorCode::decodeFailure,"Invalid fixture sample range.");
      in_.clear();
      in_.seekg(44+start*metadata_.channels*2);
      std::vector<unsigned char> b(std::size_t(count)*metadata_.channels*2);
      in_.read(reinterpret_cast<char*>(b.data()),b.size());
      if(std::size_t(in_.gcount())!=b.size()) return Result<void>::failure(ErrorCode::decodeFailure,"Short fixture WAV.");
      for(int f=0;f<count;++f) for(int c=0;c<metadata_.channels;++c) out[c][f]=std::int16_t(get16(b.data()+(std::size_t(f)*metadata_.channels+c)*2))/32768.0f;
      return Result<void>::success();
    }
    private: std::ifstream in_;
    DecodedAudio metadata_;
  };
  class TestDecoder : public AudioDecoder {
    public:
    Result<DecodedAudio> inspect(const std::filesystem::path&,CancellationToken&,std::size_t) override {
      return Result<DecodedAudio>::failure(ErrorCode::decodeFailure,"Unused fixture inspection.");
    }
    Result<std::unique_ptr<AudioSampleReader>> openReader(const std::filesystem::path& p,CancellationToken&) override {
      auto r=std::make_unique<WavReader>(p);
      if(r->metadata().frameCount<=0) return Result<std::unique_ptr<AudioSampleReader>>::failure(ErrorCode::decodeFailure,"Invalid fixture WAV.");
      return Result<std::unique_ptr<AudioSampleReader>>::success(std::move(r));
    }
  };
  #else
  using TestDecoder=JuceStemDecoder;
  #endif
  std::vector<std::vector<float>> samples(const PreparedAudio& prepared) {
    std::vector<std::vector<float>> out(prepared.channels,std::vector<float>(std::size_t(prepared.frameCount)));
    CancellationToken token;
    std::vector<float*> planes(prepared.channels);
    for(Frame p=0;p<prepared.frameCount;p+=4096) {
      const int n=int(std::min<Frame>(4096,prepared.frameCount-p));
      for(int c=0;c<prepared.channels;++c) planes[c]=out[c].data()+p;
      REQUIRE(prepared.read(p,n,planes,token));
    }
    return out;
  }
  Frame peak(const std::vector<float>& v,Frame from=0,Frame to=0) {
    if(to==0) to=Frame(v.size());
    Frame result=from;
    for(Frame f=from;f<to;++f) if(std::abs(v[std::size_t(f)])>std::abs(v[std::size_t(result)])) result=f;
    return result;
  }
  std::size_t renders(const MediaStore& store) {
    const auto p=store.root()/".renders";
    return std::filesystem::exists(p) ? std::size_t(std::distance(std::filesystem::directory_iterator(p),std::filesystem::directory_iterator{
    })) : 0;
  }
  // Instrument the real file decoder boundary; samples still come from disk.
  class ObservedReader final : public AudioSampleReader {
    public:
    ObservedReader(std::unique_ptr<AudioSampleReader> r, int& maxRead, int stop, bool error)
    : reader_(std::move(r)),maxRead_(maxRead),stop_(stop),error_(error) {
    }
    const DecodedAudio& metadata() const noexcept override {
      return reader_->metadata();
    }
    Result<void> read(Frame start,int frames,std::span<float* const> planes,CancellationToken& token) override {
      maxRead_=std::max(maxRead_,frames);
      auto result=reader_->read(start,frames,planes,token);
      if(++reads_==stop_) {
        if(error_) return Result<void>::failure(ErrorCode::decodeFailure,"Interrupted sample source.");
        token.cancel();
      }
      return result;
    }
    private: std::unique_ptr<AudioSampleReader> reader_;
    int& maxRead_;
    int stop_,reads_=0;
    bool error_;
  };
  class ObservedDecoder final : public AudioDecoder {
    public:
    int maxRead=0,stopAfter=0;
    bool failRead=false;
    Result<DecodedAudio> inspect(const std::filesystem::path& p,CancellationToken& t,std::size_t b) override {
      return decoder_.inspect(p,t,b);
    }
    Result<std::unique_ptr<AudioSampleReader>> openReader(const std::filesystem::path& p,CancellationToken& token) override {
      auto result=decoder_.openReader(p,token);
      if(!result) return result;
      return Result<std::unique_ptr<AudioSampleReader>>::success(std::make_unique<ObservedReader>(std::move(result.value()),maxRead,stopAfter,failRead));
    }
    private: TestDecoder decoder_;
  };
}
// Catches resampling instead of pitch preservation, incorrect latency crop,
// wrong tempo ratio, and overwriting original bytes.
TEST_CASE("StretchDurationPitch","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"project");
  const auto wav=temp.root/"tone.wav";
  writeWav(wav,1,48000,48000,[](Frame f,int) {
    return .25f*float(std::sin(2*3.141592653589793*440*f/48000));
  });
  auto asset=install(store,wav,1,48000,48000);
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  auto rendered=renderer.prepare(asset,{
    {
      {
        0,0
      },{
        1,2
      }
    }
  },90,store,token);
  INFO((rendered ? "OK" : rendered.error().message));
  REQUIRE(rendered);
  REQUIRE(rendered.value().frameCount==64000);
  REQUIRE(rendered.value().sampleRate==48000);
  auto audio=samples(rendered.value());
  int crossings=0;
  for(std::size_t f=16001;f<48000;++f) if(audio[0][f-1]<=0 && audio[0][f]>0) ++crossings;
  REQUIRE(double(crossings)*48000/32000 == Catch::Approx(440).epsilon(.01));
  std::ifstream original(store.root()/asset.relativePath,std::ios::binary), external(wav,std::ios::binary);
  REQUIRE(std::string(std::istreambuf_iterator<char>(original),{
  })==std::string(std::istreambuf_iterator<char>(external),{
  }));
}
// Catches independent stem processors, per-channel padding/crop, and lost silence/offsets.
TEST_CASE("StretchLinkedGroupAlignment","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto a=temp.root/"a.wav",b=temp.root/"b.wav";
  writeWav(a,2,48000,24000,[](Frame f,int c) {
    return (f==8000 || f==16000) ? .2f*(c+1) : 0;
  });
  writeWav(b,2,48000,20000,[](Frame f,int c) {
    return (f==4000 || f==12000) ? .1f*(c+1) : 0;
  });
  const auto aa=install(store,a,2,48000,24000),bb=install(store,b,2,48000,20000);
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  StretchOptions edit;
  edit.mode=TimingMode::edit;
  auto result=renderer.prepareGroup({
    {
      aa
    },{
      bb,4000.0/48000
    }
  },{
    {
      {
        0,0
      },{
        .5,1
      }
    }
  },80,store,token,edit);
  INFO((result ? "OK" : result.error().message));
  REQUIRE(result);
  REQUIRE(result.value().channels==4);
  REQUIRE(result.value().frameCount==36000);
  auto audio=samples(result.value());
  for(int c=1;c<4;++c) {
    REQUIRE(std::abs(peak(audio[c],6000,18000)-peak(audio[0],6000,18000))<=1);
    REQUIRE(std::abs(peak(audio[c],18000,33000)-peak(audio[0],18000,33000))<=1);
  }
  REQUIRE(std::abs(audio[0][0])<1.e-6);
  REQUIRE(std::abs(audio[2][0])<1.e-6);
}
// Catches Preserve following the old imported 120 BPM map after tempo changes,
// and Reset Timing leaving a stretched duration or edited placement behind.
TEST_CASE("StretchPreserveTempoChangeAndReset","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto path=temp.root/"silence.wav";
  writeWav(path,1,48000,48000,[](Frame f,int) {
    return f>=12000 && f<24000 ? .2f : 0;
  });
  const auto asset=install(store,path,1,48000,48000);
  Project p;
  p.projectId="p";
  p.revisionId="r";
  p.tempoBpm=100;
  p.assets={
    asset
  };
  TimingGroup g;
  g.id="g";
  g.timingMap=g.importedTimingMap={
    {
      {
        0,0
      },{
        1,2
      }
    }
  };
  p.groups={
    g
  };
  Track t;
  t.id="t";
  t.assetId=asset.id;
  t.timingGroupId="g";
  t.placementBeats=t.importedPlacementBeats=3;
  t.trimEndSeconds=t.importedTrimEndSeconds=1;
  p.tracks={
    t
  };
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  ProjectHistory history(p);
  auto preserved=renderer.prepareGroup(history.current(),"g",store,token);
  REQUIRE(preserved);
  REQUIRE(preserved.value().frameCount==48000);
  REQUIRE(preserved.value().originBeats==3);
  auto audio=samples(preserved.value());
  REQUIRE(audio[0][11999]==0);
  REQUIRE(audio[0][12000]==Catch::Approx(.2).margin(.0001));
  REQUIRE(audio[0][24000]==0);
  REQUIRE(history.apply(SetGroupTimingMap{
    "g",{
      {
        {
          0,0
        },{
          1,2
        }
      }
    },TimingMode::edit
  }));
  auto edited=renderer.prepareGroup(history.current(),"g",store,token);
  REQUIRE(edited);
  REQUIRE(edited.value().frameCount==57600);
  REQUIRE(history.apply(MoveGroup{
    "g",2
  }));
  REQUIRE(history.apply(TrimTrack{
    "t",.3,.8
  }));
  REQUIRE(history.apply(ResetTiming{
    "g"
  }));
  auto reset=renderer.prepareGroup(history.current(),"g",store,token);
  REQUIRE(reset);
  REQUIRE(reset.value().frameCount==48000);
  REQUIRE(reset.value().originBeats==3);
  REQUIRE(samples(reset.value())==audio);
}
TEST_CASE("StretchShortPiecewiseAndRepeatedEdits","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto path=temp.root/"short.wav";
  writeWav(path,1,48000,100,[](Frame f,int) {
    return f==40 ? .3f : 0;
  });
  const auto asset=install(store,path,1,48000,100);
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  auto shortClip=renderer.prepare(asset,{
    {
      {
        0,0
      },{
        1,2
      }
    }
  },60,store,token);
  REQUIRE(shortClip);
  REQUIRE(shortClip.value().frameCount==200);
  auto s=samples(shortClip.value());
  REQUIRE(std::abs(s[0][peak(s[0])])>.05);
  auto longPath=temp.root/"long.wav";
  writeWav(longPath,1,48000,96000,[](Frame f,int) {
    return .2f*float(std::sin(2*3.141592653589793*440*f/48000));
  });
  auto longer=install(store,longPath,1,48000,96000);
  auto piece=renderer.prepare(longer,{
    {
      {
        0,0
      },{
        1,2
      },{
        2,3
      }
    }
  },120,store,token);
  REQUIRE(piece);
  REQUIRE(piece.value().frameCount==72000);
  auto first=samples(piece.value());
  auto repeat=renderer.prepare(longer,{
    {
      {
        0,0
      },{
        1,2
      },{
        2,3
      }
    }
  },120,store,token);
  REQUIRE(repeat);
  REQUIRE(repeat.value().dataPath()!=piece.value().dataPath());
  REQUIRE(samples(repeat.value())==first);
  for(auto range:std::vector<std::pair<int,int>>{
    {
      19200,38400
    },{
      52800,67200
    }
  }) {
    int crossings=0;
    for(int f=range.first+1;f<range.second;++f) if(first[0][f-1]<=0 && first[0][f]>0) ++crossings;
    REQUIRE(double(crossings)*48000/(range.second-range.first)==Catch::Approx(440).epsilon(.01));
  }
  for(const auto& plane:first) for(float v:plane) REQUIRE(std::isfinite(v));
}
TEST_CASE("StretchLimitsCancellationAndCacheLifetime","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto path=temp.root/"tone.wav";
  writeWav(path,1,48000,48000,[](Frame,int){
    return .1f;
  });
  auto asset=install(store,path,1,48000,48000);
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  auto bad=renderer.prepare(asset,{
    {
      {
        0,0
      },{
        1,200
      }
    }
  },120,store,token);
  REQUIRE_FALSE(bad);
  REQUIRE(bad.error().code==ErrorCode::invalidTimingMap);
  REQUIRE(renders(store)==0);
  StretchOptions o;
  o.maxPreparedBytes=100;
  auto limited=renderer.prepare(asset,{
    {
      {
        0,0
      },{
        1,2
      }
    }
  },120,store,token,o);
  REQUIRE_FALSE(limited);
  REQUIRE(limited.error().code==ErrorCode::storageLimit);
  REQUIRE(renders(store)==0);
  token.cancel();
  auto cancelled=renderer.prepare(asset,{
    {
      {
        0,0
      },{
        1,2
      }
    }
  },120,store,token);
  REQUIRE_FALSE(cancelled);
  REQUIRE(cancelled.error().code==ErrorCode::cancelled);
  REQUIRE(renders(store)==0);
  CancellationToken fresh;
  std::filesystem::path preparedPath;
  {
    auto result=renderer.prepare(asset,{
      {
        {
          0,0
        },{
          1,2
        }
      }
    },120,store,fresh);
    REQUIRE(result);
    preparedPath=result.value().dataPath();
    REQUIRE(std::filesystem::exists(preparedPath));
    REQUIRE(renders(store)==1);
  }
  REQUIRE_FALSE(std::filesystem::exists(preparedPath));
  REQUIRE(renders(store)==0);
}
TEST_CASE("StretchMixedRatesOffsetsTrimsAndPreparedReads","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto a=temp.root/"a.wav",b=temp.root/"b.wav";
  writeWav(a,1,44100,44100,[](Frame f,int){
    return f>=11025 && f<22050 ? .3f : 0;
  });
  writeWav(b,1,48000,24000,[](Frame f,int){
    return f>=6000 && f<12000 ? .2f : 0;
  });
  auto aa=install(store,a,1,44100,44100),bb=install(store,b,1,48000,24000);
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  auto result=renderer.prepareGroup({
    {
      aa,0,.3,.45
    },{
      bb,.125
    }
  },{
    {
      {
        0,0
      },{
        1,2
      }
    }
  },100,store,token);
  REQUIRE(result);
  REQUIRE(result.value().sampleRate==48000);
  REQUIRE(result.value().frameCount==48000);
  const auto sourcePath=store.root()/aa.relativePath;
  const auto movedPath=sourcePath.parent_path()/"unavailable";
  std::filesystem::rename(sourcePath,movedPath);
  auto audio=samples(result.value());
  REQUIRE(audio[0][14399]==0);
  REQUIRE(audio[0][14400]==Catch::Approx(.3).margin(.0001));
  REQUIRE(audio[0][21600]==0);
  REQUIRE(audio[1][11999]==0);
  REQUIRE(audio[1][12000]==Catch::Approx(.2).margin(.0001));
  REQUIRE(audio[1][18000]==0);
  std::vector<float*> planes{
    audio[0].data(),audio[1].data()
  };
  auto bad=result.value().read(47999,2,planes,token);
  REQUIRE_FALSE(bad);
  REQUIRE(bad.error().code==ErrorCode::readFailure);
  std::filesystem::rename(movedPath,sourcePath);
}
TEST_CASE("StretchCancellationAndFailedSamplesRemovePartialRender","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto path=temp.root/"tone.wav";
  writeWav(path,1,48000,144000,[](Frame f,int){
    return .2f*float(std::sin(2*3.141592653589793*440*f/48000));
  });
  auto asset=install(store,path,1,48000,144000);
  for(bool readFailure:{
    false,true
  }) {
    ObservedDecoder decoder;
    decoder.stopAfter=2;
    decoder.failRead=readFailure;
    StretchRenderer renderer(decoder);
    CancellationToken token;
    auto result=renderer.prepare(asset,{
      {
        {
          0,0
        },{
          3,6
        }
      }
    },90,store,token);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code==(readFailure ? ErrorCode::decodeFailure : ErrorCode::cancelled));
    REQUIRE(renders(store)==0);
    REQUIRE(std::filesystem::exists(store.root()/asset.relativePath));
  }
}
TEST_CASE("StretchFractionalRoundingAndBoundedReads","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto path=temp.root/"tone.wav";
  writeWav(path,1,48000,480001,[](Frame f,int){
    return .2f*float(std::sin(2*3.141592653589793*440*f/48000));
  });
  auto asset=install(store,path,1,48000,480001);
  ObservedDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  StretchOptions options;
  options.mode=TimingMode::edit;
  options.blockFrames=257;
  auto result=renderer.prepare(asset,{
    {
      {
        0,0
      },{
        1,2
      }
    }
  },137,store,token,options);
  REQUIRE(result);
  REQUIRE(result.value().frameCount==420439);
  REQUIRE(decoder.maxRead<=4097);
  REQUIRE(std::filesystem::file_size(result.value().dataPath())==1681756);
}
TEST_CASE("StretchMapOriginAndProjectPlacement","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto path=temp.root/"tone.wav";
  writeWav(path,1,48000,48000,[](Frame f,int){
    return f==12000 ? .2f : 0;
  });
  auto asset=install(store,path,1,48000,48000);
  Project p;
  p.projectId="p";
  p.revisionId="r";
  p.tempoBpm=120;
  p.assets={
    asset
  };
  TimingGroup g;
  g.id="g";
  g.mode=TimingMode::edit;
  g.timingMap=g.importedTimingMap={
    {
      {
        0,1
      },{
        1,3
      }
    }
  };
  p.groups={
    g
  };
  Track t;
  t.id="a";
  t.assetId=asset.id;
  t.timingGroupId="g";
  t.placementBeats=t.importedPlacementBeats=3;
  t.trimEndSeconds=t.importedTrimEndSeconds=1;
  p.tracks={
    t
  };
  t.id="b";
  t.placementBeats=t.importedPlacementBeats=3.5;
  p.tracks.push_back(t);
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  auto result=renderer.prepareGroup(p,"g",store,token);
  REQUIRE(result);
  REQUIRE(result.value().originBeats==4);
  REQUIRE(result.value().frameCount==60000);
  auto audio=samples(result.value());
  // Placement delta .5 beat at120 BPM is12000frames. Pitch-preserving processing
  // may reshape absolute peaks; placement here is evaluated within one STFT block.
  REQUIRE(std::abs((peak(audio[1])-peak(audio[0]))-12000)<5760);
  auto mapOnly=renderer.prepare(asset,g.timingMap,120,store,token);
  REQUIRE(mapOnly);
  REQUIRE(mapOnly.value().originBeats==1);
  p.groups[0].timingMap={{{1.e308,1.e308},{1.000000001e308,1.7e308}}};
  // A document can contain finite monotonic markers whose extrapolation
  // overflows. Preparation must return a typed error rather than throw.
  auto overflow=renderer.prepareGroup(p,"g",store,token);
  REQUIRE_FALSE(overflow);
  REQUIRE(overflow.error().code==ErrorCode::invalidTimingMap);
}
TEST_CASE("StretchRejectsUnsafeSourcesAndSubframeSegments","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  auto path=temp.root/"tone.wav";
  writeWav(path,1,48000,48000,[](Frame,int){
    return .1f;
  });
  auto asset=install(store,path,1,48000,48000);
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  auto mismatch=asset;
  mismatch.frameCount=47999;
  auto metadata=renderer.prepare(mismatch,{
    {
      {
        0,0
      },{
        1,2
      }
    }
  },120,store,token);
  REQUIRE_FALSE(metadata);
  REQUIRE(metadata.error().code==ErrorCode::decodeFailure);
  REQUIRE(renders(store)==0);
  auto unsafe=asset;
  unsafe.relativePath="../../tone.wav";
  auto outside=renderer.prepare(unsafe,{
    {
      {
        0,0
      },{
        1,2
      }
    }
  },120,store,token);
  REQUIRE_FALSE(outside);
  REQUIRE(outside.error().code==ErrorCode::readFailure);
  // Strictly monotonic in seconds but both markers occupy the same source frame.
  auto tiny=renderer.prepare(asset,{
    {
      {
        0,0
      },{
        .000001,.000002
      },{
        1,2
      }
    }
  },120,store,token);
  REQUIRE_FALSE(tiny);
  REQUIRE(tiny.error().code==ErrorCode::invalidTimingMap);
  REQUIRE(renders(store)==0);
}
TEST_CASE("StretchHandlesSourceEdgeImpulsesAndOneFrameClip","[stretch]") {
  Temp temp;
  MediaStore store(temp.root/"p");
  TestDecoder decoder;
  StretchRenderer renderer(decoder);
  CancellationToken token;
  for(Frame frames:{
    Frame(1),Frame(10000)
  }) {
    auto path=temp.root/(std::to_string(frames)+".wav");
    writeWav(path,2,48000,frames,[frames](Frame f,int c){
      return f==0 || f==frames-1 ? .2f*(c+1) : 0;
    });
    auto asset=install(store,path,2,48000,frames);
    auto result=renderer.prepare(asset,{
      {
        {
          0,0
        },{
          1,2
        }
      }
    },60,store,token);
    REQUIRE(result);
    REQUIRE(result.value().frameCount==frames*2);
    auto audio=samples(result.value());
    if(frames==1) REQUIRE(std::abs(audio[0][peak(audio[0])])>.01);
    else {
      REQUIRE(std::abs(audio[0][peak(audio[0],0,4000)])>.01);
      REQUIRE(std::abs(audio[0][peak(audio[0],16000,20000)])>.01);
      REQUIRE(std::abs(peak(audio[0],16000,20000)-peak(audio[1],16000,20000))<=1);
    }
  }
}
