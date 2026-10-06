#include "audio/OutputRecorder.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
using namespace jeff::daw;
TEST_CASE("Live recorder writes stereo PCM without callback file IO", "[recording]") {
  auto dir =
      std::filesystem::current_path() /
      ("jds-record-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  auto path = dir / "take.wav";
  OutputRecorder recorder;
  REQUIRE(recorder.start(path, 48000));
  float left[128], right[128];
  std::fill_n(left, 128, .5f);
  std::fill_n(right, 128, -.5f);
  float *channels[]{left, right};
  recorder.capture(channels, 2, 128, 48000);
  REQUIRE(recorder.stop());
  std::ifstream in(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), {});
  in.close();
  REQUIRE(bytes.substr(0, 4) == "RIFF");
  REQUIRE(bytes.size() == 44 + 128 * 6);
  REQUIRE(bytes.substr(44, 3) != bytes.substr(47, 3));
  REQUIRE_FALSE(recorder.start(path, 48000));
  std::filesystem::remove_all(dir);
}
TEST_CASE("Live recorder rejects a changed device rate without publishing partial take",
          "[recording]") {
  auto dir = std::filesystem::current_path() /
             ("jds-record-rate-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  auto path = dir / "take.wav";
  OutputRecorder recorder;
  REQUIRE(recorder.start(path, 48000));
  float data[16]{};
  float *channels[]{data};
  recorder.capture(channels, 1, 16, 44100);
  REQUIRE_FALSE(recorder.stop());
  REQUIRE_FALSE(std::filesystem::exists(path));
  std::filesystem::remove_all(dir);
}
