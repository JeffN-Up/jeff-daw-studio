#include "stems/StretchRenderer.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
namespace jeff::daw {
  namespace {
    constexpr int sourceBlock = 4096;
    bool within(const std::filesystem::path& root, const std::filesystem::path& path) {
      std::error_code ec;
      const auto r=std::filesystem::weakly_canonical(root,ec);
      if(ec) return false;
      const auto p=std::filesystem::weakly_canonical(path,ec);
      if(ec) return false;
      const auto relative=p.lexically_relative(r);
      return !relative.empty() && !relative.is_absolute() && *relative.begin()!="..";
    }
    Result<void> available(const std::filesystem::path& root,std::uint64_t bytes,std::uint64_t reserve) {
      std::error_code ec;
      const auto space=std::filesystem::space(root,ec);
      if(ec) return Result<void>::failure(ErrorCode::writeFailure,"Cannot check render storage: "+ec.message());
      if(space.available<reserve || bytes>space.available-reserve) return Result<void>::failure(ErrorCode::storageLimit,"Insufficient project storage for prepared audio.");
      return Result<void>::success();
    }
    Frame rounded(long double v) {
      if(!std::isfinite(v) || v<0 || v>static_cast<long double>(std::numeric_limits<Frame>::max()/256)) throw std::overflow_error("Render frame range exceeds supported storage arithmetic.");
      return static_cast<Frame>(std::llround(v));
    }
    Id renderId() {
      std::random_device random;
      std::ostringstream out;
      out<<std::hex;
      for(int i=0;i<4;++i) out<<random()<<'-';
      return out.str();
    }
    std::vector<float*> pointers(std::vector<std::vector<float>>& planes) {
      std::vector<float*> out;
      for(auto& p:planes) out.push_back(p.data());
      return out;
    }
    struct Source {
      StretchMember member;
      std::unique_ptr<AudioSampleReader> reader;
      int firstChannel=0;
      Frame cacheStart=-1;
      int cacheFrames=0;
      std::vector<std::vector<float>> cache;
      std::vector<float*> planes;
      Result<void> fill(Frame start,int count,int rate,std::vector<std::vector<float>>& output,CancellationToken& token) {
        const auto& info=reader->metadata();
        auto sourceBoundary=[&](double seconds) {
          long double frame=static_cast<long double>(seconds)*info.sourceRate;
          return std::abs(frame-std::round(frame))<1.e-7L ? std::round(frame) : frame;
        };
        const auto trimStart=sourceBoundary(member.trimStartSeconds);
        const auto trimEnd=member.trimEndSeconds==0 ? static_cast<long double>(info.frameCount) : sourceBoundary(member.trimEndSeconds);
        for(int f=0;f<count;++f) {
          if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled,"Render cancelled.");
          long double position=(static_cast<long double>(start+f)/rate-member.sourceOffsetSeconds)*info.sourceRate;
          if(std::abs(position-std::round(position))<1.e-7L) position=std::round(position);
          if(position<0 || position>=info.frameCount || position<trimStart || position>=trimEnd) continue;
          const auto frame=static_cast<Frame>(std::floor(position)),next=std::min(frame+1,info.frameCount-1);
          if(cacheStart<0 || frame<cacheStart || next>=cacheStart+cacheFrames) {
            cacheStart=(frame/sourceBlock)*sourceBlock;
            cacheFrames=int(std::min<Frame>(sourceBlock+1,info.frameCount-cacheStart));
            auto read=reader->read(cacheStart,cacheFrames,planes,token);
            if(!read) return read;
            for(const auto& p:cache) for(int i=0;i<cacheFrames;++i) if(!std::isfinite(p[i])) return Result<void>::failure(ErrorCode::decodeFailure,"Source contains nonfinite samples.");
          }
          const auto fraction=float(position-frame);
          for(int c=0;c<info.channels;++c) output[firstChannel+c][f]=std::lerp(cache[c][std::size_t(frame-cacheStart)],next>=trimEnd ? 0.0f : cache[c][std::size_t(next-cacheStart)],fraction);
        }
        return Result<void>::success();
      }
    };
  }
  struct PreparedAudio::Storage {
    std::filesystem::path root,directory,path;
    Result<void> discard() {
      if(directory.empty()) return Result<void>::success();
      if(!within(root,directory)) return Result<void>::failure(ErrorCode::writeFailure,"Render cleanup refused a directory outside the project: "+directory.string());
      std::error_code ec;
      std::filesystem::remove_all(directory,ec);
      if(ec) return Result<void>::failure(ErrorCode::writeFailure,"Cannot remove prepared render "+directory.string()+": "+ec.message());
      directory.clear();
      return Result<void>::success();
    }
    ~Storage() {
      try { discard(); } catch(...) {} // Best-effort, never throws on retirement.
    }
    // Worker-owned lifetime, documented in the public type.
  };
  const std::filesystem::path& PreparedAudio::dataPath() const noexcept {
    static const std::filesystem::path empty;
    return storage_ ? storage_->path : empty;
  }
  Result<void> PreparedAudio::read(Frame start,int frames,std::span<float* const> planes,CancellationToken& token) const {
    if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled,"Prepared read cancelled.");
    if(!storage_ || channels<=0 || channels>64 || frames<0 || start<0 || start>frameCount-frames || planes.size()!=std::size_t(channels) || std::any_of(planes.begin(),planes.end(),[](auto p){
      return p==nullptr;
    })) return Result<void>::failure(ErrorCode::readFailure,"Invalid prepared sample range or channel planes.");
    std::ifstream in(storage_->path,std::ios::binary);
    if(!in) return Result<void>::failure(ErrorCode::readFailure,"Cannot open prepared audio.");
    in.seekg(start*channels*Frame(sizeof(float)));
    if(!in) return Result<void>::failure(ErrorCode::readFailure,"Cannot seek prepared audio.");
    std::vector<float> interleaved(std::size_t(sourceBlock)*channels);
    for(int done=0;done<frames;) {
      if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled,"Prepared read cancelled.");
      const int n=std::min(sourceBlock,frames-done);
      const auto bytes=std::streamsize(n)*channels*sizeof(float);
      in.read(reinterpret_cast<char*>(interleaved.data()),bytes);
      if(in.gcount()!=bytes || in.bad()) return Result<void>::failure(ErrorCode::readFailure,"Prepared audio is incomplete.");
      for(int f=0;f<n;++f) for(int c=0;c<channels;++c) planes[c][done+f]=interleaved[std::size_t(f)*channels+c];
      done+=n;
    }
    return Result<void>::success();
  }
  Result<PreparedAudio> StretchRenderer::prepare(const AudioAsset& a,const TimingMap& m,double bpm,MediaStore& s,CancellationToken& t) {
    StretchOptions o;
    o.mode=TimingMode::edit;
    return prepare(a,m,bpm,s,t,o);
  }
  Result<PreparedAudio> StretchRenderer::prepare(const AudioAsset& a,const TimingMap& m,double bpm,MediaStore& s,CancellationToken& t,const StretchOptions& o) {
    return prepareGroup({
      {
        a
      }
    },m,bpm,s,t,o);
  }
  Result<PreparedAudio> StretchRenderer::prepareGroup(const std::vector<StretchMember>& members,const TimingMap& map,double bpm,MediaStore& store,CancellationToken& token,const StretchOptions& options) {
    using R=Result<PreparedAudio>;
    std::shared_ptr<PreparedAudio::Storage> storage;
    std::ofstream out;
    auto fail=[&](ErrorCode code,std::string message) {
      if(out.is_open()) out.close();
      if(storage) {
        auto removed=storage->discard();
        if(!removed) message+=" Cleanup requires retry at "+storage->directory.string()+": "+removed.error().message;
      }
      return R::failure(code,std::move(message));
    };
    try {
      if(token.isCancelled()) return fail(ErrorCode::cancelled,"Render cancelled.");
      if(members.empty()
          || members.size()>64
          || options.maxChannels<1
          || options.maxChannels>64
          || options.blockFrames<64
          || options.blockFrames>16384
          || !std::isfinite(options.maxDurationSeconds)
          || options.maxDurationSeconds<=0
          || options.maxDurationSeconds>8*60*60
          || options.maxPreparedBytes==0
          || !std::isfinite(bpm)
          || bpm<=0
          || options.renderRate<0
          || (options.mode!=TimingMode::preserve && options.mode!=TimingMode::edit && options.mode!=TimingMode::autoSync)) return fail(ErrorCode::invalidCommand,"Invalid render configuration or group resource limits.");
      if(options.mode!=TimingMode::preserve) {
        if(map.markers.size()>4096) return fail(ErrorCode::invalidTimingMap,"Timing map exceeds the prepared marker limit.");
        auto valid=validate(map);
        if(!valid) return fail(valid.error().code,valid.error().message);
        for(std::size_t i=1;i<map.markers.size();++i) {
          const auto& a=map.markers[i-1];
          const auto& b=map.markers[i];
          const double ratio=(b.destinationBeats-a.destinationBeats)*60/bpm/(b.sourceSeconds-a.sourceSeconds);
          if(!std::isfinite(ratio) || ratio<.25 || ratio>4) return fail(ErrorCode::invalidTimingMap,"Stretch duration ratio must be between 0.25 and 4.");
        }
      }
      int channels=0,rate=options.renderRate;
      long double duration=0;
      for(const auto& m:members) {
        const auto& a=m.asset;
        if(a.channels<=0
            || a.channels>options.maxChannels-channels
            || a.sourceRate<8000
            || a.sourceRate>192000
            || a.frameCount<=0
            || !std::isfinite(m.sourceOffsetSeconds)
            || m.sourceOffsetSeconds<0
            || !std::isfinite(m.trimStartSeconds)
            || !std::isfinite(m.trimEndSeconds)
            || m.trimStartSeconds<0
            || m.trimStartSeconds>=double(a.frameCount)/a.sourceRate
            || (m.trimEndSeconds!=0 && (m.trimEndSeconds<=m.trimStartSeconds
            || m.trimEndSeconds>double(a.frameCount)/a.sourceRate))) return fail(ErrorCode::decodeFailure,"Invalid source metadata, trim, offset or group channel limit.");
        const std::filesystem::path relative(a.relativePath);
        if(relative.empty() || relative.is_absolute() || !within(store.root(),store.root()/relative)) return fail(ErrorCode::readFailure,"Source audio path resolves outside the project.");
        channels+=a.channels;
        if(options.renderRate==0) rate=std::max(rate,a.sourceRate);
        duration=std::max(duration,static_cast<long double>(m.sourceOffsetSeconds)+static_cast<long double>(a.frameCount)/a.sourceRate);
      }
      if(rate<8000 || rate>192000) return fail(ErrorCode::invalidCommand,"Prepared rate must be between 8000 and 192000 Hz.");
      if(duration>options.maxDurationSeconds) return fail(ErrorCode::storageLimit,"Source exceeds the prepared duration limit.");
      const auto sourceFrames=rounded(duration*rate);
      const double zeroBeat=options.mode==TimingMode::preserve ? 0 : mapTime(map,0);
      if(options.mode!=TimingMode::preserve) {
        Frame previousSource=-1,previousDestination=-1;
        for(const auto& marker:map.markers) {
          const Frame source=rounded(static_cast<long double>(marker.sourceSeconds)*rate);
          const Frame target=rounded((static_cast<long double>(marker.destinationBeats)-zeroBeat)*60/bpm*rate);
          if(source<=previousSource || target<=previousDestination) return fail(ErrorCode::invalidTimingMap,"Timing segments must span distinct source and destination frames.");
          previousSource=source;
          previousDestination=target;
        }
      }
      auto destination=[&](Frame f)->long double {
        return options.mode==TimingMode::preserve ? static_cast<long double>(f) : (static_cast<long double>(mapTime(map,double(f)/rate))-zeroBeat)*60/bpm*rate;
      };
      const auto outputFrames=rounded(destination(sourceFrames));
      if(outputFrames<=0 || static_cast<long double>(outputFrames)/rate>options.maxDurationSeconds) return fail(ErrorCode::storageLimit,"Destination exceeds the prepared duration limit.");
      const auto bytes=static_cast<std::uint64_t>(outputFrames)*channels*sizeof(float);
      if(bytes>options.maxPreparedBytes
          || bytes>std::uint64_t(std::numeric_limits<std::streamoff>::max())) return fail(ErrorCode::storageLimit,"Prepared audio exceeds the configured disk size limit.");
      std::error_code ec;
      std::filesystem::create_directories(store.root()/".renders",ec);
      if(ec || !within(store.root(),store.root()/".renders")) return fail(ErrorCode::writeFailure,"Cannot create project render storage.");
      auto space=available(store.root(),bytes,options.reserveBytes);
      if(!space) return fail(space.error().code,space.error().message);
      std::vector<Source> sources;
      PreparedAudio result;
      result.channels=channels;
      result.sampleRate=rate;
      result.frameCount=outputFrames;
      result.originBeats=zeroBeat;
      int firstChannel=0;
      for(const auto& m:members) {
        auto opened=decoder_.openReader(store.root()/m.asset.relativePath,token);
        if(!opened) return fail(opened.error().code,opened.error().message);
        if(!opened.value()) return fail(ErrorCode::decodeFailure,"Decoder returned an empty sample reader.");
        const auto& actual=opened.value()->metadata();
        if(actual.channels!=m.asset.channels
            || actual.sourceRate!=m.asset.sourceRate
            || actual.frameCount!=m.asset.frameCount) return fail(ErrorCode::decodeFailure,"Source metadata differs from the imported asset.");
        Source source;
        source.member=m;
        source.reader=std::move(opened.value());
        source.firstChannel=firstChannel;
        source.cache.resize(actual.channels,std::vector<float>(sourceBlock+1));
        source.planes=pointers(source.cache);
        result.members.push_back({
          m.asset.id,m.trackId,firstChannel,actual.channels
        });
        firstChannel+=actual.channels;
        sources.push_back(std::move(source));
      }
      storage=std::make_shared<PreparedAudio::Storage>();
      storage->root=store.root();
      for(int attempt=0;attempt<16;++attempt) {
        const auto candidate=store.root()/".renders"/renderId();
        if(std::filesystem::create_directory(candidate,ec)) {
          storage->directory=candidate;
          break;
        }
        if(ec) return fail(ErrorCode::writeFailure,"Cannot allocate render directory: "+ec.message());
      }
      if(storage->directory.empty()) return fail(ErrorCode::duplicateId,"Cannot allocate an exclusive render identity.");
      storage->path=storage->directory/"audio.partial";
      out.open(storage->path,std::ios::binary|std::ios::trunc);
      if(!out) return fail(ErrorCode::writeFailure,"Cannot write prepared audio.");
      const int block=options.blockFrames;
      std::vector<std::vector<float>> input(channels,std::vector<float>(block));
      const auto inputPointers=pointers(input);
      std::vector<float> interleaved(std::size_t(block)*channels);
      Frame written=0;
      auto write=[&](const std::vector<std::vector<float>>& audio,int begin,int count)->Result<void> {
        if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled,"Render cancelled.");
        for(int done=0;done<count;) {
          const int n=std::min(block,count-done);
          for(int f=0;f<n;++f) for(int c=0;c<channels;++c) {
            const auto v=audio[c][begin+done+f];
            if(!std::isfinite(v)) return Result<void>::failure(ErrorCode::decodeFailure,"Renderer produced nonfinite samples.");
            interleaved[std::size_t(f)*channels+c]=v;
          }
          auto free=available(store.root(),std::uint64_t(n)*channels*sizeof(float),options.reserveBytes);
          if(!free) return free;
          out.write(reinterpret_cast<const char*>(interleaved.data()),std::streamsize(n)*channels*sizeof(float));
          if(!out) return Result<void>::failure(ErrorCode::writeFailure,"Prepared write failed.");
          written+=n;
          done+=n;
        }
        return Result<void>::success();
      };
      auto fill=[&](Frame start,int count)->Result<void> {
        for(auto& p:input) std::fill(p.begin(),p.end(),0);
        for(auto& s:sources) {
          auto filled=s.fill(start,count,rate,input,token);
          if(!filled) return filled;
        }
        return Result<void>::success();
      };
      if(options.mode==TimingMode::preserve) {
        for(Frame start=0;start<sourceFrames;) {
          const int n=int(std::min<Frame>(block,sourceFrames-start));
          auto filled=fill(start,n);
          if(!filled) return fail(filled.error().code,filled.error().message);
          auto saved=write(input,0,n);
          if(!saved) return fail(saved.error().code,saved.error().message);
          start+=n;
        }
      }
      else {
        signalsmith::stretch::SignalsmithStretch<float> stretch(123);
        stretch.presetDefault(channels,rate,false);
        stretch.setTransposeSemitones(0);
        stretch.reset();
        const auto& a=map.markers[0];
        const auto& b=map.markers[1];
        const double initialRatio=(b.destinationBeats-a.destinationBeats)*60/bpm/(b.sourceSeconds-a.sourceSeconds);
        const auto& y=map.markers[map.markers.size()-2];
        const auto& z=map.markers.back();
        const double finalRatio=(z.destinationBeats-y.destinationBeats)*60/bpm/(z.sourceSeconds-y.sourceSeconds);
        const int seekFrames=stretch.outputSeekLength(float(1/initialRatio));
        const int padding=std::max(stretch.seekLength(),stretch.outputSeekLength(4.0f))+stretch.blockSamples();
        const auto paddedStart=-Frame(padding),paddedEnd=sourceFrames+padding;
        auto paddedDestination=[&](Frame f)->long double {
          if(f<0) return static_cast<long double>(f)*initialRatio;
          if(f>sourceFrames) return destination(sourceFrames)+static_cast<long double>(f-sourceFrames)*finalRatio;
          return destination(f);
        };
        const auto baseline=paddedDestination(paddedStart+seekFrames);
        const Frame total=rounded(paddedDestination(paddedEnd)-paddedDestination(paddedStart)),crop=rounded(-paddedDestination(paddedStart));
        // Shared bounded silence prefix and crop: upstream finite alignment with
        // the remaining body processed incrementally, never a full audio buffer.
        std::vector<std::vector<float>> prefix(channels,std::vector<float>(seekFrames));
        stretch.outputSeek(pointers(prefix),seekFrames);
        const int capacity=std::max(block*4+64,int(std::ceil(seekFrames*initialRatio))+64);
        std::vector<std::vector<float>> output(channels,std::vector<float>(capacity));
        auto outputPointers=pointers(output);
        Frame produced=0;
        auto croppedWrite=[&](int n)->Result<void> {
          const Frame begin=std::max<Frame>(produced,crop),end=std::min<Frame>(produced+n,crop+outputFrames);
          auto saved=end>begin ? write(output,int(begin-produced),int(end-begin)) : Result<void>::success();
          produced+=n;
          return saved;
        };
        std::vector<Frame> boundaries;
        for(const auto& m:map.markers) {
          const auto f=rounded(static_cast<long double>(m.sourceSeconds)*rate);
          if(f>0 && f<sourceFrames) boundaries.push_back(f);
        }
        boundaries.push_back(sourceFrames);
        boundaries.push_back(paddedEnd);
        Frame consumed=paddedStart+seekFrames,bodyProduced=0;
        while(consumed<paddedEnd) {
          if(token.isCancelled()) return fail(ErrorCode::cancelled,"Render cancelled.");
          const auto boundary=std::upper_bound(boundaries.begin(),boundaries.end(),consumed);
          const Frame end=std::min(consumed+block,*boundary);
          const int n=int(end-consumed);
          const auto cumulative=rounded(paddedDestination(end)-baseline);
          const int m=int(cumulative-bodyProduced);
          if(m<0 || m>capacity) return fail(ErrorCode::invalidTimingMap,"Timing segment exceeds the bounded output block.");
          auto filled=fill(consumed,n);
          if(!filled) return fail(filled.error().code,filled.error().message);
          stretch.process(inputPointers,n,outputPointers,m);
          auto saved=croppedWrite(m);
          if(!saved) return fail(saved.error().code,saved.error().message);
          consumed=end;
          bodyProduced=cumulative;
        }
        const auto tail=total-produced;
        if(tail<0 || tail>capacity) return fail(ErrorCode::invalidTimingMap,"Latency tail exceeds its bounded buffer.");
        stretch.flush(outputPointers,int(tail),float(1/finalRatio));
        auto saved=croppedWrite(int(tail));
        if(!saved) return fail(saved.error().code,saved.error().message);
      }
      if(token.isCancelled()) return fail(ErrorCode::cancelled,"Render cancelled.");
      if(written!=outputFrames) return fail(ErrorCode::writeFailure,"Prepared audio did not reach its requested frame length.");
      out.close();
      if(!out) return fail(ErrorCode::writeFailure,"Cannot finish prepared write.");
      const auto published=storage->directory/"audio.f32";
      std::filesystem::rename(storage->path,published,ec);
      if(ec) return fail(ErrorCode::writeFailure,"Cannot publish prepared audio: "+ec.message());
      storage->path=published;
      if(token.isCancelled()) return fail(ErrorCode::cancelled,"Render cancelled.");
      result.storage_=std::move(storage);
      return R::success(std::move(result));
    }
    catch(const std::overflow_error& e) {
      return fail(ErrorCode::storageLimit,e.what());
    }
    catch(const std::exception& e) {
      return fail(ErrorCode::decodeFailure,"Audio preparation failed: "+std::string(e.what()));
    }
  }
  Result<PreparedAudio> StretchRenderer::prepareGroup(const Project& p,const Id& groupId,MediaStore& store,CancellationToken& token,const StretchOptions& options) {
    using R=Result<PreparedAudio>;
    try {
      if(token.isCancelled()) return R::failure(ErrorCode::cancelled,"Render cancelled.");
      auto valid=validate(p);
      if(!valid) return R::failure(valid.error().code,valid.error().message);
      const auto group=std::find_if(p.groups.begin(),p.groups.end(),[&](const auto& g){
        return g.id==groupId;
      });
      if(group==p.groups.end()) return R::failure(ErrorCode::missingEntity,"Timing group does not exist.");
      std::vector<StretchMember> members;
      auto groupOrigin=groupOriginBeats(p,groupId);
      if(!groupOrigin) return R::failure(groupOrigin.error().code,groupOrigin.error().message);
      const double origin=groupOrigin.value();
      auto effective=options;
      effective.mode=group->mode;
      const double startBeat=group->mode==TimingMode::preserve ? 0 : mapTime(group->timingMap,0);
      for(const auto& t:p.tracks) {
        if(t.timingGroupId!=groupId) continue;
        const auto asset=std::find_if(p.assets.begin(),p.assets.end(),[&](const auto& a){
          return a.id==t.assetId;
        });
        auto offset=sourceGridOffset(p,t);
        if(!offset) return R::failure(offset.error().code,offset.error().message);
        members.push_back({
          *asset,offset.value(),t.trimStartSeconds,t.trimEndSeconds,t.id
        });
      }
      auto prepared=prepareGroup(members,group->timingMap,p.tempoBpm,store,token,effective);
      if(prepared) prepared.value().originBeats=origin+startBeat;
      return prepared;
    } catch(const std::overflow_error& e) {
      return R::failure(ErrorCode::invalidTimingMap,e.what());
    } catch(const std::invalid_argument& e) {
      return R::failure(ErrorCode::invalidTimingMap,e.what());
    } catch(const std::exception& e) {
      return R::failure(ErrorCode::decodeFailure,"Group preparation failed: "+std::string(e.what()));
    }
  }
} // namespace jeff::daw
