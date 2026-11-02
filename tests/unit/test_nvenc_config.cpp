/**
 * @file tests/unit/test_nvenc_config.cpp
 * @brief Test the NVENC preset handling in src/nvenc/nvenc_config.h and src/video.cpp.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <string>
#include <variant>
#include <vector>

// local includes
#include <src/config.h>
#include <src/nvenc/nvenc_config.h>
#include <src/video.h>

namespace {

  /**
   * @brief Find the `preset` option in a list of encoder options.
   *
   * @param options Encoder options to search.
   * @return Pointer to the matching option, or nullptr when there is none.
   */
  const video::encoder_t::option_t *find_preset(const std::vector<video::encoder_t::option_t> &options) {
    for (const auto &option : options) {
      if (option.name == "preset") {
        return &option;
      }
    }
    return nullptr;
  }

}  // namespace

TEST(NvencPresetTest, QualityMapsToFfmpegName) {
  for (int quality = 1; quality <= 7; ++quality) {
    EXPECT_EQ(nvenc::ffmpeg_preset_from_quality(quality), "p" + std::to_string(quality));
  }
}

TEST(NvencPresetTest, QualityBelowRangeIsClampedToFastest) {
  EXPECT_EQ(nvenc::ffmpeg_preset_from_quality(0), "p1");
  EXPECT_EQ(nvenc::ffmpeg_preset_from_quality(-5), "p1");
}

TEST(NvencPresetTest, QualityAboveRangeIsClampedToSlowest) {
  EXPECT_EQ(nvenc::ffmpeg_preset_from_quality(8), "p7");
  EXPECT_EQ(nvenc::ffmpeg_preset_from_quality(100), "p7");
}

TEST(NvencPresetTest, DefaultQualityIsFastest) {
  const nvenc::nvenc_config defaults;
  EXPECT_EQ(nvenc::ffmpeg_preset_from_quality(defaults.quality_preset), "p1");
}

TEST(NvencPresetTest, EncoderOptionsPassThePresetByName) {
  const std::array codecs {
    &video::nvenc.av1,
    &video::nvenc.hevc,
    &video::nvenc.h264,
  };

  for (const auto *codec : codecs) {
    const auto *preset = find_preset(codec->common_options);
    ASSERT_NE(preset, nullptr) << "missing preset option for " << codec->name;

    // A numeric option would depend on FFmpeg's enum numbering, which changes between releases.
    ASSERT_TRUE(std::holds_alternative<std::string *>(preset->value)) << codec->name;
    EXPECT_EQ(std::get<std::string *>(preset->value), &config::video.nv_legacy.preset) << codec->name;
  }
}
