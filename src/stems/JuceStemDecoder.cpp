#include "stems/JuceStemDecoder.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <limits>
#include <algorithm>
namespace jeff::daw {
  namespace {
    // PCM readers zero-pad short byte reads. Keep that distinct from successful
    // decoding; compressed readers may legitimately request a final partial block.
    class CheckedFileStream final : public juce::InputStream {
      public:
      explicit CheckedFileStream(const juce::File& file) : file_(file) {
      }
      juce::int64 getTotalLength() override {
        return file_.getTotalLength();
      }
      juce::int64 getPosition() override {
        return file_.getPosition();
      }
      bool setPosition(juce::int64 p) override {
        return file_.setPosition(p);
      }
      bool isExhausted() override {
        return file_.isExhausted();
      }
      int read(void* dest,int requested) override {
        const int got=file_.read(dest,requested);
        shortRead=shortRead || got<requested;
        return got;
      }
      bool failed() const {
        return file_.getStatus().failed();
      }
      bool shortRead=false;
      private: juce::FileInputStream file_;
    };
    class JuceSampleReader final : public AudioSampleReader {
      public:
      JuceSampleReader(std::unique_ptr<juce::AudioFormatReader> reader,CheckedFileStream* stream,bool pcm)
      : reader_(std::move(reader)),stream_(stream),pcm_(pcm) {
        metadata_.channels=int(reader_->numChannels);
        metadata_.sourceRate=int(reader_->sampleRate);
        metadata_.frameCount=reader_->lengthInSamples;
      }
      const DecodedAudio& metadata() const noexcept override {
        return metadata_;
      }
      Result<void> read(Frame start,int frames,std::span<float* const> planes,CancellationToken& token) override {
        if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled,"Decode cancelled.");
        if(start<0 || frames<0 || frames>16384 || start>metadata_.frameCount-frames || planes.size()!=std::size_t(metadata_.channels) || std::any_of(planes.begin(),planes.end(),[](auto p){
          return p==nullptr;
        })) return Result<void>::failure(ErrorCode::decodeFailure,"Invalid bounded sample-reader request.");
        stream_->shortRead=false;
        if(!reader_->read(planes.data(),metadata_.channels,start,frames)
            || stream_->failed()
            || (pcm_ && stream_->shortRead)) return Result<void>::failure(ErrorCode::decodeFailure,"Audio data is incomplete or decoding failed.");
        for(auto* plane:planes) for(int f=0;f<frames;++f) if(!std::isfinite(plane[f])) return Result<void>::failure(ErrorCode::decodeFailure,"Audio contains nonfinite samples.");
        if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled,"Decode cancelled.");
        return Result<void>::success();
      }
      private:
      std::unique_ptr<juce::AudioFormatReader> reader_;
      // Also owns stream_, no dangling session handle.
      CheckedFileStream* stream_;
      bool pcm_;
      DecodedAudio metadata_;
    };
  }
  Result<std::unique_ptr<AudioSampleReader>> JuceStemDecoder::openReader(const std::filesystem::path& path,CancellationToken& token) {
    using R=Result<std::unique_ptr<AudioSampleReader>>;
    if(token.isCancelled()) return R::failure(ErrorCode::cancelled,"Decode cancelled.");
    const auto utf8=path.u8string();
    auto stream=std::make_unique<CheckedFileStream>(juce::File(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()),int(utf8.size()))));
    if(stream->failed()) return R::failure(ErrorCode::readFailure,"Cannot open original audio.");
    auto* checked=stream.get();
    juce::AudioFormatManager formats;
    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    const auto wavName=wav.getFormatName(),aiffName=aiff.getFormatName();
    formats.registerFormat(new juce::WavAudioFormat,true);
    formats.registerFormat(new juce::AiffAudioFormat,false);
    formats.registerFormat(new juce::FlacAudioFormat,false);
    formats.registerFormat(new juce::MP3AudioFormat,false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
    if(!reader) return R::failure(ErrorCode::decodeFailure,"Unsupported or corrupt audio; expected WAV, AIFF, FLAC or MP3.");
    if(!std::isfinite(reader->sampleRate)
        || reader->sampleRate<=0
        || reader->sampleRate>std::numeric_limits<int>::max()
        || std::floor(reader->sampleRate)!=reader->sampleRate
        || reader->numChannels==0
        || reader->numChannels>64
        || reader->lengthInSamples<=0) return R::failure(ErrorCode::decodeFailure,"Audio has invalid source metadata or unsupported channel/sample-rate values.");
    const bool pcm=reader->getFormatName()==wavName || reader->getFormatName()==aiffName;
    return R::success(std::make_unique<JuceSampleReader>(std::move(reader),checked,pcm));
  }
  Result<DecodedAudio> JuceStemDecoder::inspect(const std::filesystem::path& path,CancellationToken& token,std::size_t maxBins) {
    using R=Result<DecodedAudio>;
    if(token.isCancelled()) return R::failure(ErrorCode::cancelled,"Decode cancelled.");
    if(maxBins==0 || maxBins>4096) return R::failure(ErrorCode::decodeFailure,"Waveform bin limit must be between 1 and 4096.");
    auto opened=openReader(path,token);
    if(!opened) return R::failure(opened.error().code,opened.error().message);
    auto& reader=*opened.value();
    DecodedAudio audio=reader.metadata();
    const auto bins=std::min<std::size_t>(maxBins,std::size_t(audio.frameCount));
    audio.waveform.resize(bins);
    for(auto& b:audio.waveform) {
      b.minimum=std::numeric_limits<float>::infinity();
      b.maximum=-std::numeric_limits<float>::infinity();
    }
    std::vector<double> squares(bins,0),counts(bins,0);
    constexpr int blockFrames=4096;
    juce::AudioBuffer<float> block(audio.channels,blockFrames);
    for(Frame start=0;start<audio.frameCount;) {
      const int frames=int(std::min<Frame>(blockFrames,audio.frameCount-start));
      auto decoded=reader.read(start,frames,std::span<float* const>(block.getArrayOfWritePointers(),audio.channels),token);
      if(!decoded) return R::failure(decoded.error().code,decoded.error().message);
      for(int f=0;f<frames;++f) {
        const auto bin=std::min(bins-1,std::size_t(static_cast<long double>(start+f)*bins/audio.frameCount));
        auto& summary=audio.waveform[bin];
        for(int c=0;c<audio.channels;++c) {
          const auto sample=block.getSample(c,f);
          summary.minimum=std::min(summary.minimum,sample);
          summary.maximum=std::max(summary.maximum,sample);
          squares[bin]+=double(sample)*sample;
          counts[bin]+=1;
        }
      }
      start+=frames;
    }
    if(token.isCancelled()) return R::failure(ErrorCode::cancelled,"Decode cancelled.");
    for(std::size_t i=0;i<bins;++i) audio.waveform[i].rms=float(std::sqrt(squares[i]/counts[i]));
    return R::success(std::move(audio));
  }
} // namespace jeff::daw
