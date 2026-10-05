#include "project/MediaStore.h"
#include <array>
#include <bit>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <algorithm>

namespace jeff::daw {
namespace {
// SHA-256 (FIPS 180-4): incremental hashing never buffers the full input.
class Sha256 {
public:
  void add(std::span<const std::byte> bytes) {
    length_ += bytes.size();
    for (const auto b : bytes) {
      buffer_[used_++] = std::to_integer<unsigned char>(b);
      if (used_ == 64) { block(); used_ = 0; }
    }
  }
  std::string finish() {
    const auto bits = length_ * 8;
    buffer_[used_++] = 0x80;
    if (used_ > 56) { std::fill(buffer_.begin() + used_, buffer_.end(), 0); block(); used_ = 0; }
    std::fill(buffer_.begin() + used_, buffer_.begin() + 56, 0);
    for (int i = 0; i < 8; ++i) buffer_[63-i] = static_cast<unsigned char>(bits >> (i*8));
    block(); std::ostringstream out; out << std::hex << std::setfill('0');
    for (const auto h : hash_) out << std::setw(8) << h;
    return out.str();
  }
private:
  void block() {
    static constexpr std::array<std::uint32_t, 64> k{
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 4; ++j) w[i] = (w[i] << 8) | buffer_[i*4+j];
    for (int i = 16; i < 64; ++i) {
      const auto s0 = std::rotr(w[i-15],7) ^ std::rotr(w[i-15],18) ^ (w[i-15] >> 3);
      const auto s1 = std::rotr(w[i-2],17) ^ std::rotr(w[i-2],19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    auto [a,b,c,d,e,f,g,h] = hash_;
    for (int i = 0; i < 64; ++i) {
      const auto t1 = h + (std::rotr(e,6) ^ std::rotr(e,11) ^ std::rotr(e,25)) + ((e&f) ^ (~e&g)) + k[i] + w[i];
      const auto t2 = (std::rotr(a,2) ^ std::rotr(a,13) ^ std::rotr(a,22)) + ((a&b) ^ (a&c) ^ (b&c));
      h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    hash_[0]+=a; hash_[1]+=b; hash_[2]+=c; hash_[3]+=d; hash_[4]+=e; hash_[5]+=f; hash_[6]+=g; hash_[7]+=h;
  }
  std::array<std::uint32_t, 8> hash_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::array<unsigned char, 64> buffer_{};
  std::uint64_t length_ = 0;
  std::size_t used_ = 0;
};
Id uniqueId() {
  std::random_device random; std::ostringstream out; out << std::hex << std::setfill('0');
  for (int i = 0; i < 4; ++i) out << std::setw(8) << static_cast<std::uint32_t>(random());
  return out.str();
}
Result<void> storageAvailable(const std::filesystem::path& root, std::uint64_t needed, std::uint64_t reserve) {
  std::error_code ec; const auto space = std::filesystem::space(root, ec);
  if (ec) return Result<void>::failure(ErrorCode::writeFailure, "Cannot check project storage: " + ec.message());
  if (space.available < reserve || needed > space.available - reserve)
    return Result<void>::failure(ErrorCode::storageLimit, "Not enough free project storage.");
  return Result<void>::success();
}
bool withinRoot(const std::filesystem::path& root, const std::filesystem::path& path) {
  std::error_code ec;
  const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec); if (ec) return false;
  const auto canonicalPath = std::filesystem::weakly_canonical(path, ec); if (ec) return false;
  const auto relative = canonicalPath.lexically_relative(canonicalRoot);
  return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
}

MediaStore::MediaStore(std::filesystem::path root, MediaStoreLimits limits)
  : root_(std::filesystem::absolute(std::move(root)).lexically_normal()), limits_(limits) {}
MediaStore::StagedMedia::StagedMedia(std::filesystem::path root, std::filesystem::path directory, Id id)
  : root_(std::move(root)), directory_(std::move(directory)), path_(directory_ / "original"), id_(std::move(id)) {}
MediaStore::StagedMedia::StagedMedia(StagedMedia&& other) noexcept { *this = std::move(other); }
MediaStore::StagedMedia& MediaStore::StagedMedia::operator=(StagedMedia&& other) noexcept {
  if (this != &other) {
    clean(); root_ = std::move(other.root_); directory_ = std::move(other.directory_); path_ = std::move(other.path_);
    id_ = std::move(other.id_); checksum_ = std::move(other.checksum_); bytes_ = other.bytes_; other.directory_.clear();
  }
  return *this;
}
void MediaStore::StagedMedia::clean() noexcept {
  if (!directory_.empty()) {
    if (withinRoot(root_, directory_)) { std::error_code ec; std::filesystem::remove_all(directory_, ec); }
    directory_.clear();
  }
}
MediaStore::StagedMedia::~StagedMedia() { clean(); }

Result<MediaStore::StagedMedia> MediaStore::stage(const InputStreamFactory& source, CancellationToken& token,
                                               std::function<void(std::uint64_t)> progress) {
  using R = Result<StagedMedia>;
  if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "Import cancelled.");
  if (!source.open) return R::failure(ErrorCode::readFailure, "Input provider has no stream factory.");
  if (source.expectedBytes && *source.expectedBytes > limits_.maxFileBytes)
    return R::failure(ErrorCode::storageLimit, "File exceeds the configured import size limit.");
  std::error_code ec;
  std::filesystem::create_directories(root_ / ".import", ec);
  if (ec) return R::failure(ErrorCode::writeFailure, "Cannot create media staging storage: " + ec.message());
  if (!withinRoot(root_, root_ / ".import")) return R::failure(ErrorCode::writeFailure, "Media staging storage resolves outside the project.");
  if (const auto available = storageAvailable(root_, source.expectedBytes.value_or(0), limits_.reserveBytes); !available)
    return R::failure(available.error().code, available.error().message);
  Id id; std::filesystem::path directory;
  for (int attempt = 0; attempt < 16; ++attempt) {
    id = uniqueId(); directory = root_ / ".import" / id;
    if (std::filesystem::create_directory(directory, ec)) break;
    if (ec) return R::failure(ErrorCode::writeFailure, "Cannot create exclusive staging directory: " + ec.message());
    directory.clear();
  }
  if (directory.empty()) return R::failure(ErrorCode::duplicateId, "Cannot allocate unique media identity.");
  StagedMedia staged(root_, directory, std::move(id));
  auto input = source.open();
  if (!input) return R::failure(input.error().code, input.error().message);
  if (!input.value()) return R::failure(ErrorCode::readFailure, "Input provider returned an empty stream.");
  std::ofstream output(staged.path_, std::ios::binary | std::ios::trunc);
  if (!output) return R::failure(ErrorCode::writeFailure, "Cannot open staged media for writing.");
  Sha256 sha; std::array<std::byte, 65536> buffer{};
  while (true) {
    if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "Import cancelled.");
    auto read = input.value()->read(buffer);
    if (!read) return R::failure(read.error().code, read.error().message);
    const auto n = read.value();
    if (n > buffer.size()) return R::failure(ErrorCode::readFailure, "Input provider returned an invalid read size.");
    if (n == 0) break;
    if (n > limits_.maxFileBytes - staged.bytes_) return R::failure(ErrorCode::storageLimit, "File exceeds the configured import size limit.");
    if (const auto available = storageAvailable(root_, n, limits_.reserveBytes); !available)
      return R::failure(available.error().code, available.error().message);
    output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(n));
    if (!output) return R::failure(ErrorCode::writeFailure, "Staged media write failed.");
    sha.add(std::span<const std::byte>(buffer.data(), n)); staged.bytes_ += n;
    if (progress) progress(staged.bytes_);
  }
  if (token.isCancelled()) return R::failure(ErrorCode::cancelled, "Import cancelled.");
  output.close();
  if (!output) return R::failure(ErrorCode::writeFailure, "Cannot finish staged media write.");
  if (source.expectedBytes && staged.bytes_ != *source.expectedBytes)
    return R::failure(ErrorCode::readFailure, "Input provider length changed or the transfer was incomplete.");
  if (staged.bytes_ == 0) return R::failure(ErrorCode::readFailure, "Input file is empty.");
  staged.checksum_ = sha.finish();
  return R::success(std::move(staged));
}

Result<AudioAsset> MediaStore::commit(StagedMedia& staged, int channels, int rate, Frame frames, CancellationToken* token) {
  using R = Result<AudioAsset>;
  if (staged.root_ != root_ || staged.directory_.empty() || staged.checksum_.size() != 64 || channels <= 0 || rate <= 0 || frames <= 0)
    return R::failure(ErrorCode::decodeFailure, "Only validated staged audio from this media store can be committed.");
  if (!withinRoot(root_, staged.path_)) return R::failure(ErrorCode::readFailure, "Staged original resolves outside the project.");
  // Decoder inspection may occur between staging and publication. Recheck the
  // original so a replaced/modified file cannot acquire the copied checksum.
  {
    std::ifstream input(staged.path_, std::ios::binary);
    if (!input) return R::failure(ErrorCode::readFailure, "Cannot verify the staged original.");
    Sha256 sha; std::uint64_t total = 0; std::array<std::byte, 65536> buffer{};
    while (input) {
      if (token && token->isCancelled()) return R::failure(ErrorCode::cancelled, "Import cancelled.");
      input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
      const auto n = static_cast<std::size_t>(input.gcount());
      if (n > staged.bytes_ - total) return R::failure(ErrorCode::readFailure, "Staged original changed after copying.");
      sha.add(std::span<const std::byte>(buffer.data(), n)); total += n;
    }
    if (!input.eof() || input.bad() || total != staged.bytes_ || sha.finish() != staged.checksum_)
      return R::failure(ErrorCode::readFailure, "Staged original checksum or length changed after copying.");
  }
  if (token && token->isCancelled()) return R::failure(ErrorCode::cancelled, "Import cancelled.");
  std::error_code ec; std::filesystem::create_directories(root_ / "media", ec);
  if (ec) return R::failure(ErrorCode::writeFailure, "Cannot create project media folder: " + ec.message());
  if (!withinRoot(root_, root_ / "media")) return R::failure(ErrorCode::writeFailure, "Media storage resolves outside the project.");
  const auto destination = root_ / "media" / staged.id_;
  if (std::filesystem::exists(destination, ec) || ec) return R::failure(ErrorCode::duplicateId, "Media identity is already installed or inaccessible.");
  std::filesystem::rename(staged.directory_, destination, ec);
  if (ec) return R::failure(ErrorCode::writeFailure, "Cannot commit project media: " + ec.message());
  AudioAsset asset; asset.id = staged.id_; asset.relativePath = "media/" + asset.id + "/original";
  asset.checksum = staged.checksum_; asset.channels = channels; asset.sourceRate = rate; asset.frameCount = frames;
  staged.directory_.clear();
  return R::success(std::move(asset));
}
Result<void> MediaStore::removeCommitted(const AudioAsset& asset) {
  if (asset.id.size() != 32 || !std::all_of(asset.id.begin(), asset.id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
      asset.relativePath != "media/" + asset.id + "/original")
    return Result<void>::failure(ErrorCode::invalidCommand, "Refusing removal of a path outside importer-owned media.");
  if (!withinRoot(root_, root_ / "media" / asset.id))
    return Result<void>::failure(ErrorCode::invalidCommand, "Refusing removal of media that resolves outside the project.");
  std::error_code ec; std::filesystem::remove_all(root_ / "media" / asset.id, ec);
  if (ec) return Result<void>::failure(ErrorCode::writeFailure, "Cannot roll back imported media: " + ec.message());
  return Result<void>::success();
}
} // namespace jeff::daw
