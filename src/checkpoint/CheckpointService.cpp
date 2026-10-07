#include "checkpoint/CheckpointService.h"
#include "project/ProjectSerializer.h"

#include <miniz.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace jeff::daw {
namespace {
using Json = nlohmann::json;
constexpr std::size_t ioBlock = 64 * 1024;
constexpr std::uint64_t maxDocumentBytes = 16ULL * 1024 * 1024;

class Sha256 {
public:
  void add(std::span<const std::byte> bytes) {
    length_ += bytes.size();
    for(const auto byte : bytes) {
      buffer_[used_++] = std::to_integer<unsigned char>(byte);
      if(used_ == 64) { block(); used_ = 0; }
    }
  }
  std::string finish() {
    const auto bits = length_ * 8;
    buffer_[used_++] = 0x80;
    if(used_ > 56) { std::fill(buffer_.begin() + used_, buffer_.end(), 0); block(); used_ = 0; }
    std::fill(buffer_.begin() + used_, buffer_.begin() + 56, 0);
    for(int i = 0; i < 8; ++i) buffer_[63 - i] = static_cast<unsigned char>(bits >> (i * 8));
    block();
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for(const auto value : hash_) output << std::setw(8) << value;
    return output.str();
  }
private:
  void block() {
    static constexpr std::array<std::uint32_t, 64> constants{
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<std::uint32_t, 64> words{};
    for(int i = 0; i < 16; ++i)
      for(int j = 0; j < 4; ++j) words[i] = (words[i] << 8) | buffer_[i * 4 + j];
    for(int i = 16; i < 64; ++i) {
      const auto a = std::rotr(words[i - 15], 7) ^ std::rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
      const auto b = std::rotr(words[i - 2], 17) ^ std::rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
      words[i] = words[i - 16] + a + words[i - 7] + b;
    }
    auto [a,b,c,d,e,f,g,h] = hash_;
    for(int i = 0; i < 64; ++i) {
      const auto first = h + (std::rotr(e,6) ^ std::rotr(e,11) ^ std::rotr(e,25)) +
                         ((e & f) ^ (~e & g)) + constants[i] + words[i];
      const auto second = (std::rotr(a,2) ^ std::rotr(a,13) ^ std::rotr(a,22)) +
                          ((a & b) ^ (a & c) ^ (b & c));
      h=g; g=f; f=e; e=d+first; d=c; c=b; b=a; a=first+second;
    }
    hash_[0]+=a; hash_[1]+=b; hash_[2]+=c; hash_[3]+=d;
    hash_[4]+=e; hash_[5]+=f; hash_[6]+=g; hash_[7]+=h;
  }
  std::array<std::uint32_t, 8> hash_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::array<unsigned char, 64> buffer_{};
  std::uint64_t length_ = 0;
  std::size_t used_ = 0;
};

std::string nonce() {
  return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

class PathCleanup {
public:
  explicit PathCleanup(std::filesystem::path path) : path_(std::move(path)) {}
  ~PathCleanup() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
private:
  std::filesystem::path path_;
};

std::string safeSlug(std::string_view value) {
  std::string result;
  for(const unsigned char character : value) {
    if(result.size() >= 48) break;
    result.push_back(std::isalnum(character) || character == '-' || character == '_' ?
                     char(character) : '_');
  }
  if(result.empty()) result = "project";
  std::uint64_t hash = 1469598103934665603ULL;
  for(const unsigned char character : value) { hash ^= character; hash *= 1099511628211ULL; }
  std::ostringstream suffix;
  suffix << std::hex << std::setw(16) << std::setfill('0') << hash;
  return result + "-" + suffix.str();
}

bool safeArchivePath(const std::string& name) {
  if(name.empty() || name.front() == '/' || name.front() == '\\' ||
     name.find('\\') != std::string::npos || name.find(':') != std::string::npos ||
     name.find('\0') != std::string::npos) return false;
  std::size_t start = 0;
  while(start < name.size()) {
    const auto end = name.find('/', start);
    const auto part = name.substr(start, end == std::string::npos ? end : end - start);
    if(part.empty() || part == "." || part == "..") return false;
    if(end == std::string::npos) return true;
    start = end + 1;
  }
  return false;
}

bool withinRoot(const std::filesystem::path& root, const std::filesystem::path& path) {
  std::error_code error;
  const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
  if(error) return false;
  const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
  if(error) return false;
  const auto relative = canonicalPath.lexically_relative(canonicalRoot);
  return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

Result<std::string> sha256File(const std::filesystem::path& path, CancellationToken& token,
                               std::uint64_t maximum) {
  std::ifstream input(path, std::ios::binary);
  if(!input) return Result<std::string>::failure(ErrorCode::readFailure, "Cannot open media for hashing.");
  Sha256 hash;
  std::array<std::byte, ioBlock> buffer{};
  std::uint64_t total = 0;
  while(input) {
    if(token.isCancelled()) return Result<std::string>::failure(ErrorCode::cancelled, "Checkpoint cancelled.");
    input.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(buffer.size()));
    const auto count = std::size_t(input.gcount());
    if(count > maximum - total)
      return Result<std::string>::failure(ErrorCode::storageLimit, "Checkpoint media exceeds size limits.");
    hash.add(std::span<const std::byte>(buffer.data(), count));
    total += count;
  }
  if(!input.eof() || input.bad())
    return Result<std::string>::failure(ErrorCode::readFailure, "Cannot finish media hashing.");
  return Result<std::string>::success(hash.finish());
}

class ZipWriter {
public:
  explicit ZipWriter(const std::filesystem::path& path) {
    ready_ = mz_zip_writer_init_file(&zip_, path.string().c_str(), 0) != 0;
  }
  ~ZipWriter() { mz_zip_writer_end(&zip_); }
  bool ready() const noexcept { return ready_; }
  bool addMemory(const std::string& name, std::string_view data) {
    return mz_zip_writer_add_mem(&zip_, name.c_str(), data.data(), data.size(), MZ_BEST_SPEED) != 0;
  }
  bool addFile(const std::string& name, const std::filesystem::path& path) {
    return mz_zip_writer_add_file(&zip_, name.c_str(), path.string().c_str(), nullptr, 0,
                                  MZ_NO_COMPRESSION) != 0;
  }
  bool finish() { return mz_zip_writer_finalize_archive(&zip_) != 0; }
private:
  mz_zip_archive zip_{};
  bool ready_ = false;
};

class ZipReader {
public:
  explicit ZipReader(const std::filesystem::path& path) {
    ready_ = mz_zip_reader_init_file(&zip_, path.string().c_str(), 0) != 0;
  }
  ~ZipReader() { mz_zip_reader_end(&zip_); }
  bool ready() const noexcept { return ready_; }
  mz_uint entries() noexcept { return mz_zip_reader_get_num_files(&zip_); }
  bool stat(mz_uint index, mz_zip_archive_file_stat& value) {
    return mz_zip_reader_file_stat(&zip_, index, &value) != 0;
  }
  Result<std::string> text(const char* name, std::uint64_t maximum) {
    size_t size = 0;
    void* memory = mz_zip_reader_extract_file_to_heap(&zip_, name, &size, 0);
    if(!memory) return Result<std::string>::failure(ErrorCode::readFailure,
                                                    std::string("Cannot extract ") + name + ".");
    if(size > maximum) { mz_free(memory); return Result<std::string>::failure(
        ErrorCode::storageLimit, std::string(name) + " exceeds the size limit."); }
    std::string result(static_cast<const char*>(memory), size);
    mz_free(memory);
    return Result<std::string>::success(std::move(result));
  }
  bool extract(const std::string& name, const std::filesystem::path& path) {
    return mz_zip_reader_extract_file_to_file(&zip_, name.c_str(), path.string().c_str(), 0) != 0;
  }
private:
  mz_zip_archive zip_{};
  bool ready_ = false;
};

struct MediaManifest {
  std::string path;
  std::string checksum;
  std::uint64_t bytes = 0;
};

Result<std::vector<MediaManifest>> parseManifest(const std::string& text, const Project& project) {
  try {
    const auto manifest = Json::parse(text);
    if(!manifest.is_object() || manifest.at("format") != "jeff-daw-checkpoint" ||
       manifest.at("schemaVersion").get<int>() != 1 ||
       manifest.at("projectId").get<std::string>() != project.projectId ||
       manifest.at("revisionId").get<std::string>() != project.revisionId ||
       manifest.value("parentRevisionId", std::string{}) != project.parentRevisionId)
      throw std::runtime_error("Checkpoint manifest does not match the project.");
    const auto& media = manifest.at("media");
    if(!media.is_array() || media.size() != project.assets.size())
      throw std::runtime_error("Checkpoint media list is incomplete.");
    std::map<std::string, const AudioAsset*> expected;
    for(const auto& asset : project.assets) expected.emplace(asset.relativePath, &asset);
    std::vector<MediaManifest> result;
    for(const auto& value : media) {
      MediaManifest item{value.at("path").get<std::string>(),
                         value.at("sha256").get<std::string>(),
                         value.at("bytes").get<std::uint64_t>()};
      const auto found = expected.find(item.path);
      if(found == expected.end() || found->second->checksum != item.checksum ||
         !safeArchivePath(item.path))
        throw std::runtime_error("Checkpoint media identity does not match the project.");
      expected.erase(found);
      result.push_back(std::move(item));
    }
    if(!expected.empty()) throw std::runtime_error("Checkpoint media list is incomplete.");
    return Result<std::vector<MediaManifest>>::success(std::move(result));
  } catch(const std::exception& error) {
    return Result<std::vector<MediaManifest>>::failure(ErrorCode::invalidProject,
      "Invalid checkpoint manifest: " + std::string(error.what()));
  }
}

Result<void> promote(const std::filesystem::path& staging,
                     const std::filesystem::path& destination, bool replace) {
  std::error_code error;
  if(!replace) {
    std::filesystem::rename(staging, destination, error);
    if(error) return Result<void>::failure(ErrorCode::writeFailure,
                                           "Cannot install checkpoint: " + error.message());
    return Result<void>::success();
  }
  const auto backup = destination.parent_path() /
      (destination.filename().string() + ".recovery");
  std::filesystem::remove_all(backup, error);
  if(error) return Result<void>::failure(ErrorCode::writeFailure,
                                         "Cannot rotate project recovery: " + error.message());
  std::filesystem::rename(destination, backup, error);
  if(error) return Result<void>::failure(ErrorCode::writeFailure,
                                         "Cannot preserve current project: " + error.message());
  std::filesystem::rename(staging, destination, error);
  if(error) {
    std::error_code restore;
    std::filesystem::rename(backup, destination, restore);
    return Result<void>::failure(ErrorCode::writeFailure,
                                 "Cannot install checkpoint: " + error.message());
  }
  return Result<void>::success();
}
} // namespace

LocalProjectStore::LocalProjectStore(std::filesystem::path root, CheckpointLimits limits)
    : root_(std::filesystem::absolute(std::move(root)).lexically_normal()), limits_(limits) {}

std::filesystem::path LocalProjectStore::primaryPath(const Id& projectId) const {
  return root_ / ("project-" + safeSlug(projectId));
}

Result<CheckpointInfo> CheckpointService::exportCheckpoint(
    const Project& project, MediaStore& mediaStore, OutputTarget& target,
    CancellationToken& token, ExportProgressCallback progress, OverwritePolicy overwrite) const {
  using R = Result<CheckpointInfo>;
  if(const auto valid = validate(project); !valid)
    return R::failure(valid.error().code, valid.error().message);
  if(token.isCancelled()) return R::failure(ErrorCode::cancelled, "Checkpoint cancelled.");
  auto projectText = serializeProject(project);
  if(!projectText) return R::failure(projectText.error().code, projectText.error().message);
  std::error_code error;
  const auto stagingRoot = mediaStore.root() / ".checkpoint";
  std::filesystem::create_directories(stagingRoot, error);
  if(error || !withinRoot(mediaStore.root(), stagingRoot))
    return R::failure(ErrorCode::writeFailure, "Cannot create safe checkpoint staging storage.");
  const auto packagePath = stagingRoot / ("export-" + nonce() + ".jdsproject");
  PathCleanup packageCleanup(packagePath);
  auto cleanup = [&] { std::error_code ignored; std::filesystem::remove(packagePath, ignored); };
  Json media = Json::array();
  std::vector<std::pair<std::string, std::filesystem::path>> files;
  std::set<std::string> mediaPaths;
  std::uint64_t totalMedia = 0;
  for(const auto& asset : project.assets) {
    if(token.isCancelled()) { cleanup(); return R::failure(ErrorCode::cancelled, "Checkpoint cancelled."); }
    if(!mediaPaths.insert(asset.relativePath).second) {
      cleanup(); return R::failure(ErrorCode::duplicateId, "Project assets must use distinct media paths.");
    }
    const auto source = mediaStore.root() / std::filesystem::path(asset.relativePath);
    if(!safeArchivePath(asset.relativePath) || !withinRoot(mediaStore.root(), source) ||
       !std::filesystem::is_regular_file(source, error) || error) {
      cleanup(); return R::failure(ErrorCode::readFailure, "Project media is missing or unsafe: " + asset.relativePath);
    }
    const auto bytes = std::filesystem::file_size(source, error);
    if(error || bytes > limits_.maxExtractedBytes - totalMedia) {
      cleanup(); return R::failure(ErrorCode::storageLimit, "Checkpoint media exceeds size limits.");
    }
    auto checksum = sha256File(source, token, limits_.maxExtractedBytes);
    if(!checksum) { cleanup(); return R::failure(checksum.error().code, checksum.error().message); }
    if(checksum.value() != asset.checksum) {
      cleanup(); return R::failure(ErrorCode::readFailure, "Project media checksum changed: " + asset.relativePath);
    }
    totalMedia += bytes;
    media.push_back({{"path", asset.relativePath}, {"sha256", checksum.value()}, {"bytes", bytes}});
    files.emplace_back(asset.relativePath, source);
  }
  Json manifest{{"format", "jeff-daw-checkpoint"}, {"schemaVersion", 1},
                {"projectId", project.projectId}, {"revisionId", project.revisionId},
                {"parentRevisionId", project.parentRevisionId}, {"media", std::move(media)}};
  {
    ZipWriter zip(packagePath);
    if(!zip.ready() || !zip.addMemory("project.json", projectText.value()) ||
       !zip.addMemory("manifest.json", manifest.dump(2))) {
      cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot start checkpoint package.");
    }
    for(const auto& [name, path] : files) {
      if(token.isCancelled()) { cleanup(); return R::failure(ErrorCode::cancelled, "Checkpoint cancelled."); }
      if(!zip.addFile(name, path)) {
        cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot add project media to checkpoint.");
      }
    }
    if(!zip.finish()) { cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot finish checkpoint package."); }
  }
  const auto packageBytes = std::filesystem::file_size(packagePath, error);
  if(error || packageBytes > limits_.maxPackageBytes) {
    cleanup(); return R::failure(ErrorCode::storageLimit, "Checkpoint package exceeds size limits.");
  }
  const auto preferred = safeSlug(project.projectId) + ".jdsproject";
  auto opened = target.openStaged(preferred, overwrite);
  if(!opened) { cleanup(); return R::failure(opened.error().code, opened.error().message); }
  auto staged = std::move(opened.value());
  auto fail = [&](ErrorCode code, std::string message) {
    if(staged.stream) staged.stream->discard();
    cleanup();
    return R::failure(code, std::move(message));
  };
  if(!staged.stream) return fail(ErrorCode::writeFailure, "Output target returned no staged stream.");
  std::ifstream input(packagePath, std::ios::binary);
  if(!input) return fail(ErrorCode::readFailure, "Cannot reopen checkpoint package.");
  std::array<std::byte, ioBlock> buffer{};
  std::uint64_t copied = 0;
  while(input) {
    if(token.isCancelled()) return fail(ErrorCode::cancelled, "Checkpoint cancelled.");
    input.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(buffer.size()));
    const auto count = std::size_t(input.gcount());
    if(count) {
      auto written = staged.stream->write(std::span<const std::byte>(buffer.data(), count));
      if(!written) return fail(written.error().code, written.error().message);
      copied += count;
      if(progress) progress(double(copied) / double(packageBytes));
    }
  }
  if(!input.eof() || input.bad()) return fail(ErrorCode::readFailure, "Cannot read checkpoint package.");
  auto committed = staged.stream->commit();
  if(!committed) return fail(committed.error().code, committed.error().message);
  cleanup();
  if(progress) progress(1);
  return R::success({std::move(staged.name), packageBytes, project.revisionId});
}

Result<ImportDecision> CheckpointService::importCheckpoint(
    InputStream& input, LocalProjectStore& store, CancellationToken& token) const {
  using R = Result<ImportDecision>;
  if(token.isCancelled()) return R::failure(ErrorCode::cancelled, "Checkpoint import cancelled.");
  std::error_code error;
  std::filesystem::create_directories(store.root(), error);
  if(error) return R::failure(ErrorCode::writeFailure, "Cannot create local project storage: " + error.message());
  const auto session = store.root() / (".incoming-" + nonce());
  const auto package = session / "package.jdsproject";
  const auto unpacked = session / "project";
  PathCleanup sessionCleanup(session);
  std::filesystem::create_directories(unpacked, error);
  auto cleanup = [&] { std::error_code ignored; std::filesystem::remove_all(session, ignored); };
  if(error || !withinRoot(store.root(), session)) {
    cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot create safe import staging storage.");
  }
  {
    std::ofstream output(package, std::ios::binary | std::ios::trunc);
    if(!output) { cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot stage checkpoint package."); }
    std::array<std::byte, ioBlock> buffer{};
    std::uint64_t total = 0;
    while(true) {
      if(token.isCancelled()) { output.close(); cleanup(); return R::failure(ErrorCode::cancelled, "Checkpoint import cancelled."); }
      auto read = input.read(buffer);
      if(!read) { output.close(); cleanup(); return R::failure(read.error().code, read.error().message); }
      if(read.value() > buffer.size()) { output.close(); cleanup(); return R::failure(ErrorCode::readFailure, "Provider returned an invalid read size."); }
      if(read.value() == 0) break;
      if(read.value() > store.limits().maxPackageBytes - total) {
        output.close(); cleanup(); return R::failure(ErrorCode::storageLimit, "Checkpoint package exceeds size limits.");
      }
      output.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(read.value()));
      if(!output) { output.close(); cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot stage checkpoint package."); }
      total += read.value();
    }
    output.close();
    if(!output || total == 0) { cleanup(); return R::failure(ErrorCode::readFailure, "Checkpoint package is incomplete."); }
  }
  ZipReader zip(package);
  if(!zip.ready()) { cleanup(); return R::failure(ErrorCode::readFailure, "Checkpoint is not a readable ZIP package."); }
  if(zip.entries() < 2 || zip.entries() > store.limits().maxEntries) {
    cleanup(); return R::failure(ErrorCode::storageLimit, "Checkpoint has an invalid entry count.");
  }
  std::map<std::string, std::uint64_t> archiveEntries;
  std::uint64_t extractedTotal = 0;
  for(mz_uint index = 0; index < zip.entries(); ++index) {
    mz_zip_archive_file_stat stat{};
    if(!zip.stat(index, stat)) {
      cleanup(); return R::failure(ErrorCode::invalidProject, "Checkpoint contains an invalid entry.");
    }
    const auto unixType = (stat.m_external_attr >> 16) & 0170000;
    if(stat.m_is_directory || unixType == 0120000) {
      cleanup(); return R::failure(ErrorCode::invalidProject, "Checkpoint contains an invalid entry.");
    }
    const std::string name = stat.m_filename;
    if(!safeArchivePath(name) || !archiveEntries.emplace(name, stat.m_uncomp_size).second ||
       stat.m_uncomp_size > store.limits().maxExtractedBytes - extractedTotal) {
      cleanup(); return R::failure(ErrorCode::invalidProject, "Checkpoint contains unsafe or excessive paths.");
    }
    extractedTotal += stat.m_uncomp_size;
  }
  auto projectText = zip.text("project.json", maxDocumentBytes);
  auto manifestText = zip.text("manifest.json", maxDocumentBytes);
  if(!projectText || !manifestText) {
    cleanup(); const auto& failure = !projectText ? projectText.error() : manifestText.error();
    return R::failure(failure.code, failure.message);
  }
  auto project = deserializeProject(projectText.value());
  if(!project) { cleanup(); return R::failure(project.error().code, project.error().message); }
  auto manifest = parseManifest(manifestText.value(), project.value());
  if(!manifest) { cleanup(); return R::failure(manifest.error().code, manifest.error().message); }
  if(archiveEntries.size() != manifest.value().size() + 2 || !archiveEntries.contains("project.json") ||
     !archiveEntries.contains("manifest.json")) {
    cleanup(); return R::failure(ErrorCode::invalidProject, "Checkpoint contains unlisted entries.");
  }
  for(const auto& media : manifest.value()) {
    if(token.isCancelled()) { cleanup(); return R::failure(ErrorCode::cancelled, "Checkpoint import cancelled."); }
    const auto found = archiveEntries.find(media.path);
    if(found == archiveEntries.end() || found->second != media.bytes) {
      cleanup(); return R::failure(ErrorCode::readFailure, "Checkpoint media length does not match its manifest.");
    }
    const auto destination = unpacked / std::filesystem::path(media.path);
    std::filesystem::create_directories(destination.parent_path(), error);
    if(error || !withinRoot(unpacked, destination) || !zip.extract(media.path, destination)) {
      cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot extract checkpoint media safely.");
    }
    auto checksum = sha256File(destination, token, store.limits().maxExtractedBytes);
    if(!checksum) { cleanup(); return R::failure(checksum.error().code, checksum.error().message); }
    if(checksum.value() != media.checksum) {
      cleanup(); return R::failure(ErrorCode::readFailure, "Checkpoint media checksum does not match its manifest.");
    }
  }
  if(!saveProject(project.value(), unpacked / "project.json")) {
    cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot stage the imported project document.");
  }

  auto destination = store.primaryPath(project.value().projectId);
  ImportDisposition disposition = ImportDisposition::installed;
  bool replace = false;
  if(std::filesystem::exists(destination, error)) {
    if(error) { cleanup(); return R::failure(ErrorCode::writeFailure, "Cannot inspect existing project."); }
    auto existing = loadProject(destination / "project.json");
    if(!existing) { cleanup(); return R::failure(existing.error().code, "Existing project is invalid: " + existing.error().message); }
    if(existing.value().projectId != project.value().projectId) {
      cleanup(); return R::failure(ErrorCode::duplicateId, "Project storage identity collision.");
    }
    if(existing.value().revisionId == project.value().revisionId) {
      cleanup(); return R::success({ImportDisposition::alreadyPresent, destination, std::move(project.value())});
    }
    if(project.value().parentRevisionId == existing.value().revisionId) {
      replace = true;
      disposition = ImportDisposition::updated;
    } else {
      disposition = ImportDisposition::conflictCopy;
      destination = store.root() / (destination.filename().string() + "-conflict-" + safeSlug(project.value().revisionId));
      if(std::filesystem::exists(destination, error)) {
        auto existingConflict = loadProject(destination / "project.json");
        if(existingConflict && existingConflict.value().revisionId == project.value().revisionId) {
          cleanup(); return R::success({ImportDisposition::alreadyPresent, destination, std::move(project.value())});
        }
        destination += "-" + nonce();
      }
    }
  }
  auto installed = promote(unpacked, destination, replace);
  if(!installed) { cleanup(); return R::failure(installed.error().code, installed.error().message); }
  cleanup();
  return R::success({disposition, std::move(destination), std::move(project.value())});
}

} // namespace jeff::daw
