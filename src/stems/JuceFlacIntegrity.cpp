#include "stems/JuceFlacIntegrity.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <memory>

#if JUCE_USE_FLAC

// This ABI/header is deliberately tied to JUCE 8.0.12's embedded libFLAC 1.4.3.
// It is the same C-linkage API and namespace compiled by juce_audio_formats;
// no additional codec binary, private JUCE reader access, or source patch.
#define FLAC__NO_DLL 1
namespace juce::FlacNamespace {
#include <juce_audio_formats/codecs/flac/stream_decoder.h>
}

namespace jeff::daw {
namespace {
namespace flac = juce::FlacNamespace;
struct Scan {
  juce::InputStream& input;
  CancellationToken& token;
  const std::function<bool()>& streamFailed;
  VerifiedFlac info;
  flac::FLAC__uint64 advertised = 0, encodedPosition = 0;
  Frame frameLimit = 0;
  unsigned bitsPerSample = 0, maxBlock = 0;
  bool metadata = false, invalid = false, readFailed = false;
};
flac::FLAC__StreamDecoderReadStatus readBytes(const flac::FLAC__StreamDecoder*,
                                             flac::FLAC__byte bytes[], std::size_t* count, void* context) {
  auto& scan = *static_cast<Scan*>(context);
  if (scan.token.isCancelled() || scan.invalid || *count == 0) {
    *count = 0;
    return flac::FLAC__STREAM_DECODER_READ_STATUS_ABORT;
  }
  const int requested = int(std::min<std::size_t>(*count, 65536));
  const int got = scan.input.read(bytes, requested);
  if (got < 0 || got > requested || scan.streamFailed()) {
    scan.readFailed = true; *count = 0;
    return flac::FLAC__STREAM_DECODER_READ_STATUS_ABORT;
  }
  *count = std::size_t(got);
  if (got > 0) return flac::FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
  if (scan.input.isExhausted()) return flac::FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
  scan.readFailed = true;
  return flac::FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}
flac::FLAC__StreamDecoderTellStatus tellPosition(const flac::FLAC__StreamDecoder*, flac::FLAC__uint64* position, void* context) {
  auto& scan = *static_cast<Scan*>(context);
  const auto offset = scan.input.getPosition();
  if (offset < 0 || scan.streamFailed()) {
    scan.readFailed = true;
    return flac::FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
  }
  *position = flac::FLAC__uint64(offset);
  return flac::FLAC__STREAM_DECODER_TELL_STATUS_OK;
}
void metadata(const flac::FLAC__StreamDecoder*, const flac::FLAC__StreamMetadata* block, void* context) {
  auto& scan = *static_cast<Scan*>(context);
  if (block->type != flac::FLAC__METADATA_TYPE_STREAMINFO || scan.metadata) { scan.invalid = true; return; }
  const auto& info = block->data.stream_info;
  // Validate STREAMINFO before decoding any frame. libFLAC's format caps a
  // decoded block at 65535 samples and 8 channels (at most 2 MiB of int32).
  if (info.channels < 1 || info.channels > 8 || info.sample_rate < 8000 || info.sample_rate > 192000 ||
      info.bits_per_sample < 4 || info.bits_per_sample > 32 || info.min_blocksize < 16 ||
      info.max_blocksize < info.min_blocksize || info.max_blocksize > 65535 ||
      info.total_samples > flac::FLAC__uint64(info.sample_rate) * 8 * 60 * 60) {
    scan.invalid = true; return;
  }
  scan.metadata = true; scan.info.channels = int(info.channels); scan.info.sampleRate = int(info.sample_rate);
  scan.advertised = info.total_samples; scan.frameLimit = Frame(info.sample_rate) * 8 * 60 * 60;
  scan.bitsPerSample = info.bits_per_sample; scan.maxBlock = info.max_blocksize;
}
flac::FLAC__StreamDecoderWriteStatus decodedFrame(const flac::FLAC__StreamDecoder* decoder,
                                                  const flac::FLAC__Frame* frame,
                                                  const flac::FLAC__int32* const[], void* context) {
  auto& scan = *static_cast<Scan*>(context);
  flac::FLAC__uint64 position = 0;
  if (scan.token.isCancelled() || scan.invalid || !scan.metadata ||
      frame->header.channels != unsigned(scan.info.channels) || frame->header.sample_rate != unsigned(scan.info.sampleRate) ||
      frame->header.bits_per_sample != scan.bitsPerSample || frame->header.blocksize == 0 || frame->header.blocksize > scan.maxBlock ||
      frame->header.number_type != flac::FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER ||
      frame->header.number.sample_number != flac::FLAC__uint64(scan.info.frames) ||
      frame->header.blocksize > scan.frameLimit - scan.info.frames ||
      !flac::FLAC__stream_decoder_get_decode_position(decoder, &position) || position <= scan.encodedPosition) {
    scan.invalid = true;
    return flac::FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  }
  // The pinned codec can repair missing complete frames with synthetic silence.
  // Such callbacks share the following real frame's encoded byte position, so
  // they fail strict progress even when STREAMINFO has no optional MD5 digest.
  scan.encodedPosition = position;
  scan.info.frames += frame->header.blocksize;
  return flac::FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}
void codecError(const flac::FLAC__StreamDecoder*, flac::FLAC__StreamDecoderErrorStatus, void* context) {
  static_cast<Scan*>(context)->invalid = true;
}
}
Result<VerifiedFlac> verifyFlac(juce::InputStream& input, CancellationToken& token,
                               const std::function<bool()>& streamFailed) {
  using R = Result<VerifiedFlac>;
  if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "FLAC integrity check cancelled.");
  if (!input.setPosition(0)) return R::failure(ErrorCode::readFailure, "Cannot rewind FLAC for integrity checking.");
  Scan scan{input, token, streamFailed};
  std::unique_ptr<flac::FLAC__StreamDecoder, decltype(&flac::FLAC__stream_decoder_delete)>
      decoder(flac::FLAC__stream_decoder_new(), &flac::FLAC__stream_decoder_delete);
  if (!decoder) return R::failure(ErrorCode::decodeFailure, "Cannot create FLAC integrity decoder.");
  if (!flac::FLAC__stream_decoder_set_md5_checking(decoder.get(), true) ||
      !flac::FLAC__stream_decoder_set_metadata_ignore_all(decoder.get()) ||
      !flac::FLAC__stream_decoder_set_metadata_respond(decoder.get(), flac::FLAC__METADATA_TYPE_STREAMINFO) ||
      flac::FLAC__stream_decoder_init_stream(decoder.get(), readBytes, nullptr, tellPosition, nullptr, nullptr,
                                            decodedFrame, metadata, codecError, &scan) != flac::FLAC__STREAM_DECODER_INIT_STATUS_OK)
    return R::failure(ErrorCode::decodeFailure, "Cannot initialise FLAC integrity checking.");
  bool complete = flac::FLAC__stream_decoder_process_until_end_of_metadata(decoder.get()) && scan.metadata;
  while (complete && !scan.invalid && !scan.readFailed && !token.isCancelled() &&
         flac::FLAC__stream_decoder_get_state(decoder.get()) != flac::FLAC__STREAM_DECODER_END_OF_STREAM) {
    const auto state = flac::FLAC__stream_decoder_get_state(decoder.get());
    if (state == flac::FLAC__STREAM_DECODER_ABORTED || state == flac::FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR) { complete = false; break; }
    complete = flac::FLAC__stream_decoder_process_single(decoder.get());
  }
  complete = complete && flac::FLAC__stream_decoder_get_state(decoder.get()) == flac::FLAC__STREAM_DECODER_END_OF_STREAM;
  // Per the bundled API: false means a nonzero stored MD5 disagrees. An absent
  // all-zero MD5 is legitimate; count/CRC/strict physical frame progress remain.
  const bool digestMatches = flac::FLAC__stream_decoder_finish(decoder.get());
  if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "FLAC integrity check cancelled.");
  if (scan.readFailed || streamFailed()) return R::failure(ErrorCode::readFailure, "FLAC integrity source read failed.");
  if (!complete || scan.invalid || !digestMatches || scan.info.frames <= 0 ||
      (scan.advertised != 0 && scan.advertised != flac::FLAC__uint64(scan.info.frames)))
    return R::failure(ErrorCode::decodeFailure, "FLAC audio is incomplete, corrupt or disagrees with STREAMINFO.");
  if (!input.setPosition(0)) return R::failure(ErrorCode::readFailure, "Cannot rewind validated FLAC.");
  return R::success(scan.info);
}
}
#else
namespace jeff::daw {
Result<VerifiedFlac> verifyFlac(juce::InputStream&, CancellationToken&, const std::function<bool()>&) {
  return Result<VerifiedFlac>::failure(ErrorCode::decodeFailure, "FLAC support is disabled in this JUCE build.");
}
}
#endif
