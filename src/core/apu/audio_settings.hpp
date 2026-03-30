#pragma once

#include <cstdint>

namespace mapperbus::core {

enum class ResamplingMode : uint8_t { CubicHermite, BlipBuffer };
enum class FilterMode : uint8_t { HardwareAccurate, Enhanced, Unfiltered };
enum class FilterProfile : uint8_t { NES, Famicom };
enum class StereoMode : uint8_t { Mono, PseudoStereo };
enum class ExpansionMixingMode : uint8_t { SimpleSum, ResistanceModeled };

struct AudioSettings {
    int sample_rate = 96000;
    int buffer_size_samples = 2048;
    ResamplingMode resampling = ResamplingMode::BlipBuffer;
    FilterMode filter_mode = FilterMode::Unfiltered;
    FilterProfile filter_profile = FilterProfile::NES;
    StereoMode stereo_mode = StereoMode::Mono;
    bool dithering_enabled = false;
    ExpansionMixingMode expansion_mixing = ExpansionMixingMode::SimpleSum;
};

} // namespace mapperbus::core
