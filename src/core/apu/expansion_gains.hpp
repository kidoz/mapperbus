#pragma once

namespace mapperbus::core::expansion_gains {

// Gain factors derived from Famicom expansion port resistance modeling.
// Internal audio path ~15kOhm, expansion port ~47kOhm.
inline constexpr float kVrc6 = 0.006f;
inline constexpr float kVrc7 = 0.02f;
inline constexpr float kMmc5 = 0.00752f;
inline constexpr float kMmc5Pcm = 0.002f;
inline constexpr float kN163 = 0.0004f;
inline constexpr float kSunsoft5b = 0.005f;
inline constexpr float kFds = 0.15f;

} // namespace mapperbus::core::expansion_gains
