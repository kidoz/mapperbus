#include "core/mappers/mapper518.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

#include "core/apu/expansion_gains.hpp"
#include "core/mappers/lpc_tables.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrg16Size = 0x004000UZ;
constexpr std::size_t kPrg32Size = 0x008000UZ;
constexpr std::size_t kCartridgeRamSize = 0x002000UZ;
constexpr std::size_t kSecondaryRamSize = 0x020000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::uint16_t kStateTag = 0x5180U;
constexpr Byte k32KMode = 0x04U;
constexpr Byte kChrAutoMode = 0x02U;
constexpr Byte kSecondaryMode = 0x80U;
constexpr Byte kExpansionMode = 0x40U;
constexpr Byte kPrgBankMask = 0x3FU;

constexpr std::array<std::int16_t, 16> kD6Energy{
    0, 1, 2, 3, 4, 5, 7, 11, 17, 26, 41, 63, 85, 112, 127, 0};
constexpr std::array<Byte, 128> kD6Pitch{
    0x0C, 0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11, 0x12, 0x12, 0x12, 0x13, 0x13, 0x14, 0x14,
    0x14, 0x15, 0x15, 0x15, 0x16, 0x16, 0x17, 0x17, 0x18, 0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B,
    0x1B, 0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1F, 0x1F, 0x20, 0x20, 0x21, 0x21, 0x22, 0x23, 0x23, 0x24,
    0x25, 0x25, 0x26, 0x27, 0x28, 0x28, 0x29, 0x2A, 0x2B, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x30,
    0x31, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3F, 0x40,
    0x41, 0x42, 0x43, 0x45, 0x46, 0x47, 0x49, 0x4A, 0x4B, 0x4D, 0x4E, 0x50, 0x51, 0x52, 0x54, 0x56,
    0x57, 0x59, 0x5A, 0x5C, 0x5E, 0x60, 0x61, 0x63, 0x65, 0x67, 0x69, 0x6B, 0x6D, 0x6F, 0x71, 0x73,
    0x75, 0x77, 0x79, 0x7C, 0x7E, 0x80, 0x82, 0x85, 0x87, 0x8A, 0x8C, 0x8F, 0x92, 0x94, 0x97, 0x9A};
constexpr std::array<std::int16_t, 64> kD6K1{
    static_cast<std::int16_t>(0x810), static_cast<std::int16_t>(0x824),
    static_cast<std::int16_t>(0x834), static_cast<std::int16_t>(0x848),
    static_cast<std::int16_t>(0x85C), static_cast<std::int16_t>(0x870),
    static_cast<std::int16_t>(0x884), static_cast<std::int16_t>(0x89C),
    static_cast<std::int16_t>(0x8B4), static_cast<std::int16_t>(0x8CC),
    static_cast<std::int16_t>(0x8E4), static_cast<std::int16_t>(0x900),
    static_cast<std::int16_t>(0x91C), static_cast<std::int16_t>(0x938),
    static_cast<std::int16_t>(0x958), static_cast<std::int16_t>(0x978),
    static_cast<std::int16_t>(0x998), static_cast<std::int16_t>(0x9D8),
    static_cast<std::int16_t>(0x9FC), static_cast<std::int16_t>(0xA20),
    static_cast<std::int16_t>(0xA44), static_cast<std::int16_t>(0xA6C),
    static_cast<std::int16_t>(0xA94), static_cast<std::int16_t>(0xAB8),
    static_cast<std::int16_t>(0xAE0), static_cast<std::int16_t>(0xB0C),
    static_cast<std::int16_t>(0xB38), static_cast<std::int16_t>(0xB64),
    static_cast<std::int16_t>(0xB90), static_cast<std::int16_t>(0xBC0),
    static_cast<std::int16_t>(0xBF4), static_cast<std::int16_t>(0xC24),
    static_cast<std::int16_t>(0xC58), static_cast<std::int16_t>(0xC8C),
    static_cast<std::int16_t>(0xCC4), static_cast<std::int16_t>(0xCFC),
    static_cast<std::int16_t>(0xD38), static_cast<std::int16_t>(0xD78),
    static_cast<std::int16_t>(0xDB4), static_cast<std::int16_t>(0xDF4),
    static_cast<std::int16_t>(0xE38), static_cast<std::int16_t>(0xE7C),
    static_cast<std::int16_t>(0xEC0), static_cast<std::int16_t>(0xF04),
    static_cast<std::int16_t>(0xF4C), static_cast<std::int16_t>(0xF9C),
    static_cast<std::int16_t>(0xFEC), static_cast<std::int16_t>(0x044),
    static_cast<std::int16_t>(0x09C), static_cast<std::int16_t>(0x0F4),
    static_cast<std::int16_t>(0x158), static_cast<std::int16_t>(0x1C8),
    static_cast<std::int16_t>(0x238), static_cast<std::int16_t>(0x2AC),
    static_cast<std::int16_t>(0x328), static_cast<std::int16_t>(0x3A8),
    static_cast<std::int16_t>(0x42C), static_cast<std::int16_t>(0x4B8),
    static_cast<std::int16_t>(0x540), static_cast<std::int16_t>(0x5C4),
    static_cast<std::int16_t>(0x650), static_cast<std::int16_t>(0x6E0),
    static_cast<std::int16_t>(0x780)};
constexpr std::array<std::int16_t, 64> kD6K2{
    static_cast<std::int16_t>(0x8A0), static_cast<std::int16_t>(0x980),
    static_cast<std::int16_t>(0xA3C), static_cast<std::int16_t>(0xADC),
    static_cast<std::int16_t>(0xB48), static_cast<std::int16_t>(0xBA8),
    static_cast<std::int16_t>(0xC00), static_cast<std::int16_t>(0xC50),
    static_cast<std::int16_t>(0xC9C), static_cast<std::int16_t>(0xCE4),
    static_cast<std::int16_t>(0xD2C), static_cast<std::int16_t>(0xD6C),
    static_cast<std::int16_t>(0xDAC), static_cast<std::int16_t>(0xDE8),
    static_cast<std::int16_t>(0xE20), static_cast<std::int16_t>(0xE5C),
    static_cast<std::int16_t>(0xE94), static_cast<std::int16_t>(0xECC),
    static_cast<std::int16_t>(0xF00), static_cast<std::int16_t>(0xF34),
    static_cast<std::int16_t>(0xF68), static_cast<std::int16_t>(0xF9C),
    static_cast<std::int16_t>(0xFD0), static_cast<std::int16_t>(0x000),
    static_cast<std::int16_t>(0x034), static_cast<std::int16_t>(0x064),
    static_cast<std::int16_t>(0x094), static_cast<std::int16_t>(0x0C4),
    static_cast<std::int16_t>(0x0F4), static_cast<std::int16_t>(0x128),
    static_cast<std::int16_t>(0x158), static_cast<std::int16_t>(0x188),
    static_cast<std::int16_t>(0x1B8), static_cast<std::int16_t>(0x1E8),
    static_cast<std::int16_t>(0x218), static_cast<std::int16_t>(0x24C),
    static_cast<std::int16_t>(0x27C), static_cast<std::int16_t>(0x2AC),
    static_cast<std::int16_t>(0x2DC), static_cast<std::int16_t>(0x30C),
    static_cast<std::int16_t>(0x340), static_cast<std::int16_t>(0x370),
    static_cast<std::int16_t>(0x3A4), static_cast<std::int16_t>(0x3D0),
    static_cast<std::int16_t>(0x400), static_cast<std::int16_t>(0x430),
    static_cast<std::int16_t>(0x460), static_cast<std::int16_t>(0x490),
    static_cast<std::int16_t>(0x4C0), static_cast<std::int16_t>(0x4F4),
    static_cast<std::int16_t>(0x524), static_cast<std::int16_t>(0x554),
    static_cast<std::int16_t>(0x584), static_cast<std::int16_t>(0x5E0),
    static_cast<std::int16_t>(0x610), static_cast<std::int16_t>(0x630),
    static_cast<std::int16_t>(0x668), static_cast<std::int16_t>(0x694),
    static_cast<std::int16_t>(0x6C0), static_cast<std::int16_t>(0x6F0),
    static_cast<std::int16_t>(0x720), static_cast<std::int16_t>(0x764),
    static_cast<std::int16_t>(0x7C0)};
constexpr std::array<Byte, 32> kD6K3{
    0x8B, 0x9A, 0xA2, 0xA9, 0xAF, 0xB5, 0xBB, 0xC0, 0xC5, 0xCA, 0xCF, 0xD4, 0xD9, 0xDE, 0xE2, 0xE7,
    0xEC, 0xF1, 0xF6, 0xFB, 0x01, 0x07, 0x0D, 0x14, 0x1A, 0x22, 0x29, 0x32, 0x3B, 0x45, 0x53, 0x6D};
constexpr std::array<Byte, 32> kD6K4{
    0x94, 0xB0, 0xC2, 0xCB, 0xD3, 0xD9, 0xDF, 0xE5, 0xEA, 0xEF, 0xF4, 0xF9, 0xFE, 0x03, 0x07, 0x0C,
    0x11, 0x15, 0x1A, 0x1F, 0x24, 0x29, 0x2E, 0x33, 0x38, 0x3E, 0x44, 0x4B, 0x53, 0x5A, 0x64, 0x74};
constexpr std::array<Byte, 16> kD6K5{
    0xA3, 0xC5, 0xD4, 0xE0, 0xEA, 0xF3, 0xFC, 0x04, 0x0C, 0x15, 0x1E, 0x27, 0x31, 0x3D, 0x4C, 0x66};
constexpr std::array<Byte, 16> kD6K6{
    0xAA, 0xD7, 0xE7, 0xF2, 0xFC, 0x05, 0x0D, 0x14, 0x1C, 0x24, 0x2D, 0x36, 0x40, 0x4A, 0x55, 0x6A};
constexpr std::array<Byte, 16> kD6K7{
    0xA3, 0xC8, 0xD7, 0xE3, 0xED, 0xF5, 0xFD, 0x05, 0x0D, 0x14, 0x1D, 0x26, 0x31, 0x3C, 0x4B, 0x67};
constexpr std::array<Byte, 8> kD6K8{0xC5, 0xE4, 0xF6, 0x05, 0x14, 0x27, 0x3E, 0x58};
constexpr std::array<Byte, 8> kD6K9{0xB9, 0xDC, 0xEC, 0xF9, 0x04, 0x10, 0x1F, 0x45};
constexpr std::array<Byte, 8> kD6K10{0xC3, 0xE6, 0xF3, 0xFD, 0x06, 0x11, 0x1E, 0x43};
constexpr std::array<unsigned, 10> kD6Widths{6, 6, 5, 5, 4, 4, 4, 3, 3, 3};

std::int16_t d6_wide_coefficient(std::int16_t value) {
    const auto signed_value = value >= 0x800 ? value - 0x1000 : value;
    return static_cast<std::int16_t>(signed_value / 4);
}

std::int16_t d6_narrow_coefficient(Byte value) {
    return static_cast<std::int16_t>(static_cast<std::int8_t>(value) * 2);
}

bool fdc_address(Address addr) {
    return addr == 0x5500U || addr == 0x5501U || addr == 0x5505U || addr == 0x5604U ||
           addr == 0x5605U;
}

} // namespace

// Functional byte-stream LPC model. D6 widths and values come from TI SPSS011D;
// the customized chip's analog path and exact arbitration timing are unpublished.
class Dance2000Speech final {
  public:
    void set_region(Region region) {
        cpu_clock_ =
            static_cast<std::uint32_t>(std::lround(timing_for_region(region).cpu_clock_hz()));
        phase_ %= cpu_clock_;
    }

    void reset() {
        const auto cpu_clock = cpu_clock_;
        *this = Dance2000Speech{};
        cpu_clock_ = cpu_clock;
    }

    bool ready() const {
        return reset_clocks_ == 0U && !awaiting_terminator_ && count_ < 64U;
    }

    Byte status() const {
        return static_cast<Byte>((ready() ? 0x80U : 0U) | energy_code_);
    }

    std::int16_t output() const {
        return output_;
    }

    void write_byte(Byte data) {
        if (awaiting_terminator_) {
            if (data == 0x0FU) {
                codec_ = Codec::None;
                awaiting_terminator_ = false;
                field_ = 0U;
            }
            return;
        }
        if (codec_ == Codec::None) {
            if (data == 0x0AU) {
                codec_ = Codec::D6;
            } else if (data == 0x0BU) {
                codec_ = Codec::Pe;
            }
            return;
        }
        if (count_ > 120U) {
            return;
        }
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const unsigned index = (head_ + count_) & 127U;
            const auto mask = static_cast<Byte>(1U << (index & 7U));
            fifo_[index / 8U] = static_cast<Byte>((fifo_[index / 8U] & ~mask) |
                                                  (((data >> bit) & 1U) != 0U ? mask : 0U));
            ++count_;
        }
    }

    void clock() {
        phase_ += 1000000U;
        if (phase_ < cpu_clock_) {
            return;
        }
        phase_ -= cpu_clock_;
        if (reset_clocks_ != 0U) {
            --reset_clocks_;
            return;
        }
        if (++divider_ == 100U) {
            divider_ = 0U;
            sample();
        }
    }

    void save_state(StateWriter& writer) const {
        writer.write<std::uint16_t>(0xD618U);
        writer.write_array(fifo_);
        writer.write(head_);
        writer.write(count_);
        writer.write(static_cast<Byte>(codec_));
        writer.write(phase_);
        writer.write(reset_clocks_);
        writer.write(divider_);
        writer.write(frame_sample_);
        writer.write(field_);
        writer.write(energy_code_);
        writer.write<Byte>((repeat_ ? 1U : 0U) | (playing_ ? 2U : 0U) | (interpolate_ ? 4U : 0U) |
                           (awaiting_terminator_ ? 8U : 0U));
        writer.write(pitch_count_);
        writer.write(noise_);
        writer.write(output_);
        save_frame(writer, current_);
        save_frame(writer, target_);
        save_frame(writer, pending_);
        writer.write_array(delay_);
    }

    void load_state(StateReader& reader) {
        auto next = *this;
        const auto tag = reader.read<std::uint16_t>();
        reader.read_array(next.fifo_);
        reader.read(next.head_);
        reader.read(next.count_);
        const auto codec = reader.read<Byte>();
        reader.read(next.phase_);
        reader.read(next.reset_clocks_);
        reader.read(next.divider_);
        reader.read(next.frame_sample_);
        reader.read(next.field_);
        reader.read(next.energy_code_);
        const auto flags = reader.read<Byte>();
        reader.read(next.pitch_count_);
        reader.read(next.noise_);
        reader.read(next.output_);
        load_frame(reader, next.current_);
        load_frame(reader, next.target_);
        load_frame(reader, next.pending_);
        reader.read_array(next.delay_);

        const bool valid_empty =
            codec != 0U || (next.count_ == 0U && next.field_ == 0U && (flags & 2U) == 0U);
        const bool valid_terminator =
            (flags & 8U) == 0U || (codec != 0U && next.count_ == 0U && (flags & 2U) == 0U);
        if (!reader.ok() || tag != 0xD618U || codec > 2U || next.head_ >= 128U ||
            next.count_ > 128U || next.phase_ >= cpu_clock_ || next.reset_clocks_ > 16000U ||
            next.divider_ >= 100U || next.frame_sample_ >= 200U || next.field_ > 12U ||
            next.energy_code_ > 15U || flags > 15U || next.noise_ == 0U || next.noise_ > 0x1FFFU ||
            (next.output_ & 255) != 0 || !valid_frame(next.current_) ||
            !valid_frame(next.target_) || !valid_frame(next.pending_) || !valid_empty ||
            !valid_terminator || ((flags & 2U) != 0U && next.field_ != 0U) ||
            (next.reset_clocks_ != 0U &&
             ((flags & 2U) != 0U || next.field_ != 0U || next.divider_ != 0U))) {
            reader.fail();
            return;
        }

        next.codec_ = static_cast<Codec>(codec);
        next.repeat_ = (flags & 1U) != 0U;
        next.playing_ = (flags & 2U) != 0U;
        next.interpolate_ = (flags & 4U) != 0U;
        next.awaiting_terminator_ = (flags & 8U) != 0U;
        *this = next;
    }

  private:
    enum class Codec : Byte { None, D6, Pe };

    struct Frame {
        std::int16_t energy = 0;
        std::int16_t pitch = 0;
        std::array<std::int16_t, 10> k{};
    };

    unsigned read_bits(unsigned width) {
        unsigned result = 0U;
        for (unsigned bit = 0U; bit < width; ++bit) {
            result = (result << 1U) | ((fifo_[head_ / 8U] >> (head_ & 7U)) & 1U);
            head_ = static_cast<Byte>((head_ + 1U) & 127U);
            --count_;
        }
        return result;
    }

    void start_frame() {
        interpolate_ = target_.energy != 0 && ((target_.pitch == 0) == (pending_.pitch == 0)) &&
                       !(target_.pitch == 0 && pending_.energy == 0);
        target_ = pending_;
        if (!interpolate_) {
            current_ = target_;
            pitch_count_ = 0U;
        }
        if (target_.pitch == 0) {
            std::fill(current_.k.begin() + 4, current_.k.end(), 0);
        }
        frame_sample_ = 0U;
        field_ = 0U;
        playing_ = true;
    }

    void stop_frame() {
        head_ = 0U;
        count_ = 0U;
        field_ = 0U;
        awaiting_terminator_ = true;
        current_ = target_ = pending_ = Frame{};
        delay_.fill(0);
        output_ = 0;
        pitch_count_ = 0U;
        repeat_ = false;
        playing_ = false;
        interpolate_ = false;
    }

    std::int16_t coefficient(unsigned stage, unsigned value) const {
        if (codec_ == Codec::Pe) {
            return lpc_tables::kReflection[stage][value];
        }
        switch (stage) {
        case 0U:
            return d6_wide_coefficient(kD6K1[value]);
        case 1U:
            return d6_wide_coefficient(kD6K2[value]);
        case 2U:
            return d6_narrow_coefficient(kD6K3[value]);
        case 3U:
            return d6_narrow_coefficient(kD6K4[value]);
        case 4U:
            return d6_narrow_coefficient(kD6K5[value]);
        case 5U:
            return d6_narrow_coefficient(kD6K6[value]);
        case 6U:
            return d6_narrow_coefficient(kD6K7[value]);
        case 7U:
            return d6_narrow_coefficient(kD6K8[value]);
        case 8U:
            return d6_narrow_coefficient(kD6K9[value]);
        default:
            return d6_narrow_coefficient(kD6K10[value]);
        }
    }

    void parse() {
        while (!playing_ && !awaiting_terminator_ && codec_ != Codec::None) {
            const unsigned width = field_ == 0U   ? 4U
                                   : field_ == 1U ? 1U
                                   : field_ == 2U
                                       ? (codec_ == Codec::D6 ? 7U : 6U)
                                       : (codec_ == Codec::D6 ? kD6Widths[field_ - 3U]
                                                              : lpc_tables::kWidths[field_ - 3U]);
            if (count_ < width) {
                return;
            }
            const unsigned value = read_bits(width);
            if (field_ == 0U) {
                pending_ = target_;
                energy_code_ = static_cast<Byte>(value);
                pending_.energy =
                    codec_ == Codec::D6 ? kD6Energy[value] : lpc_tables::kEnergy[value];
                if (value == 15U) {
                    stop_frame();
                    return;
                }
                if (value == 0U) {
                    pending_.pitch = 0;
                    start_frame();
                    return;
                }
            } else if (field_ == 1U) {
                repeat_ = value != 0U;
            } else if (field_ == 2U) {
                pending_.pitch = codec_ == Codec::D6 ? kD6Pitch[value] : lpc_tables::kPitch[value];
                if (repeat_) {
                    start_frame();
                    return;
                }
            } else {
                pending_.k[field_ - 3U] = coefficient(field_ - 3U, value);
                if (field_ == 12U || (field_ == 6U && pending_.pitch == 0)) {
                    start_frame();
                    return;
                }
            }
            ++field_;
        }
    }

    static std::int16_t wrap(std::int32_t value) {
        const auto bits = static_cast<std::uint32_t>(value) & 0x7FFFU;
        return static_cast<std::int16_t>(static_cast<std::int32_t>(bits) -
                                         ((bits & 0x4000U) != 0U ? 32768 : 0));
    }

    void sample() {
        if (!playing_) {
            parse();
        }
        if (!playing_) {
            output_ = 0;
            return;
        }
        if (interpolate_ && frame_sample_ % 25U == 0U) {
            const unsigned shift = lpc_tables::kInterpolation[frame_sample_ / 25U];
            current_.energy +=
                static_cast<std::int16_t>((target_.energy - current_.energy) >> shift);
            current_.pitch += static_cast<std::int16_t>((target_.pitch - current_.pitch) >> shift);
            for (unsigned index = 0U; index < 10U; ++index) {
                const auto coefficient = target_.pitch == 0 && index >= 4U ? 0 : target_.k[index];
                current_.k[index] +=
                    static_cast<std::int16_t>((coefficient - current_.k[index]) >> shift);
            }
        }
        const int excitation = target_.pitch == 0
                                   ? ((noise_ & 1U) != 0U ? -64 : 64)
                                   : lpc_tables::kChirp[std::min<unsigned>(pitch_count_, 51U)];
        for (unsigned index = 0U; index < 20U; ++index) {
            const auto feedback = ((noise_ >> 12U) ^ (noise_ >> 3U) ^ (noise_ >> 2U) ^ noise_) & 1U;
            noise_ = static_cast<std::uint16_t>(((noise_ << 1U) | feedback) & 0x1FFFU);
        }
        std::array<std::int16_t, 11> forward{};
        forward[10] = wrap(current_.energy * excitation / 8);
        for (unsigned index = 10U; index > 0U; --index) {
            forward[index - 1U] =
                wrap(forward[index] - ((current_.k[index - 1U] * delay_[index - 1U]) >> 9));
        }
        for (unsigned index = 9U; index > 0U; --index) {
            delay_[index] =
                wrap(delay_[index - 1U] + ((current_.k[index - 1U] * forward[index - 1U]) >> 9));
        }
        delay_[0] = forward[0];
        output_ = static_cast<std::int16_t>((std::clamp<int>(forward[0], -2048, 2047) >> 4) * 256);
        ++pitch_count_;
        if (pitch_count_ >= std::max<int>(current_.pitch, 1)) {
            pitch_count_ = 0U;
        }
        if (++frame_sample_ == 200U) {
            frame_sample_ = 0U;
            playing_ = false;
        }
    }

    static void save_frame(StateWriter& writer, const Frame& frame) {
        writer.write(frame.energy);
        writer.write(frame.pitch);
        writer.write_array(frame.k);
    }

    static void load_frame(StateReader& reader, Frame& frame) {
        reader.read(frame.energy);
        reader.read(frame.pitch);
        reader.read_array(frame.k);
    }

    static bool valid_frame(const Frame& frame) {
        return frame.energy >= 0 && frame.energy <= 127 && frame.pitch >= 0 && frame.pitch <= 160 &&
               std::ranges::all_of(
                   frame.k, [](std::int16_t value) { return value >= -512 && value <= 511; });
    }

    std::array<Byte, 16> fifo_{};
    Byte head_ = 0U;
    Byte count_ = 0U;
    Codec codec_ = Codec::None;
    std::uint32_t cpu_clock_ = 1773448U;
    std::uint32_t phase_ = 0U;
    std::uint16_t reset_clocks_ = 16000U;
    Byte divider_ = 0U;
    Byte frame_sample_ = 0U;
    Byte field_ = 0U;
    Byte energy_code_ = 0U;
    bool repeat_ = false;
    bool playing_ = false;
    bool interpolate_ = false;
    bool awaiting_terminator_ = false;
    Byte pitch_count_ = 0U;
    std::uint16_t noise_ = 0x1FFFU;
    std::int16_t output_ = 0;
    Frame current_, target_, pending_;
    std::array<std::int16_t, 10> delay_{};
};

// Original implementation from NESdev NES 2.0 Mapper 518 oldid 22909.
// Reset is unspecified: restore register defaults and header CIRAM, retain RAM on
// soft reset, and clear all volatile RAM on cold power. FDC ports provide only
// their documented register transport; no floppy command/media engine is implied.
Mapper518::Mapper518(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode), region_(header.region),
      speech_(std::make_unique<Dance2000Speech>()) {
    speech_->set_region(region_);
    power_on();
}

Mapper518::~Mapper518() = default;

Result<void> Mapper518::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 518U) {
        return std::unexpected("Mapper 518 requires mapper ID 518");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 518 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrg16Size != 0U ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 518 requires 32 KiB-2 MiB of 16 KiB-banked PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kCartridgeRamSize ||
        header.prg_nvram_size != 0U || header.chr_ram_size != kCartridgeRamSize ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 518 requires 8 KiB volatile PRG RAM and 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 518 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 518 does not provide trainer RAM");
    }
    return {};
}

Result<void> Mapper518::configure_board_profile(std::string_view profile) {
    if (profile != "cartridge" && profile != "sb97") {
        return std::unexpected("Mapper 518 board profile must be cartridge or sb97");
    }
    sb97_ = profile == "sb97";
    secondary_ram_.assign(sb97_ ? kSecondaryRamSize : 0U, 0U);
    power_on();
    return {};
}

bool Mapper518::secondary_selected(Address addr) const {
    return addr >= 0x8000U && (prg_bank_ & kSecondaryMode) != 0U &&
           (addr < 0xC000U || (mode_ & k32KMode) != 0U);
}

std::size_t Mapper518::secondary_offset(Address addr) const {
    const auto unit = (mode_ & k32KMode) != 0U ? kPrg32Size : kPrg16Size;
    const auto base = static_cast<std::size_t>(prg_bank_ & kPrgBankMask) * unit;
    return (base + (addr & (unit - 1U))) % kSecondaryRamSize;
}

Byte Mapper518::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return wram_[addr & (kCartridgeRamSize - 1U)];
    }
    if (secondary_selected(addr)) {
        return sb97_ ? secondary_ram_[secondary_offset(addr)] : 0U;
    }
    if (addr < 0x8000U) {
        return 0U;
    }
    if (addr >= 0xC000U && (mode_ & k32KMode) == 0U) {
        return prg_rom_[addr - 0xC000U];
    }
    const auto unit = (mode_ & k32KMode) != 0U ? kPrg32Size : kPrg16Size;
    const auto base =
        static_cast<std::size_t>((prg_bank_ & kExpansionMode) != 0U ? 0x100000U : 0U) +
        static_cast<std::size_t>(prg_bank_ & kPrgBankMask) * unit;
    return prg_rom_[(base + (addr & (unit - 1U))) % prg_rom_.size()];
}

void Mapper518::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        wram_[addr & (kCartridgeRamSize - 1U)] = value;
        return;
    }
    if (secondary_selected(addr) && sb97_) {
        secondary_ram_[secondary_offset(addr)] = value;
    }
}

bool Mapper518::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper518::prg_read_mask(Address addr) const {
    return maps_prg(addr) && (sb97_ || !secondary_selected(addr)) ? 0xFFU : 0U;
}

std::size_t Mapper518::chr_offset(Address addr) const {
    if (addr < kChrBankSize && (mode_ & kChrAutoMode) != 0U && ciram_a10_ != 0U) {
        return addr + kChrBankSize;
    }
    return addr;
}

Byte Mapper518::read_chr(Address addr) {
    return addr < kCartridgeRamSize ? chr_ram_[chr_offset(addr)] : 0U;
}

void Mapper518::write_chr(Address addr, Byte value) {
    if (addr < kCartridgeRamSize) {
        chr_ram_[addr] = value;
    }
}

MirrorMode Mapper518::mirror_mode() const {
    return (mode_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

bool Mapper518::observes_ppu_address() const {
    return true;
}

void Mapper518::observe_ppu_address(Address addr) {
    const auto a13 = static_cast<Byte>((addr & 0x2000U) != 0U ? 1U : 0U);
    if (a13 != 0U && ppu_a13_ == 0U) {
        ciram_a10_ = static_cast<Byte>((addr >> 10U) & 1U);
    }
    ppu_a13_ = a13;
}

bool Mapper518::maps_expansion(Address addr) const {
    return addr == 0x5000U || addr == 0x5200U || addr == 0x5300U || (sb97_ && fdc_address(addr));
}

Byte Mapper518::expansion_read_mask(Address addr) const {
    if (addr == 0x5300U) {
        return 0x8FU;
    }
    return sb97_ && (addr == 0x5604U || addr == 0x5605U) ? 0xFFU : 0U;
}

Byte Mapper518::read_expansion(Address addr) {
    if (addr == 0x5300U) {
        return speech_->status();
    }
    if (sb97_ && addr == 0x5604U) {
        return 0x80U;
    }
    if (sb97_ && addr == 0x5605U) {
        return fdc_.data;
    }
    return 0U;
}

void Mapper518::write_expansion(Address addr, Byte value) {
    switch (addr) {
    case 0x5000U:
        prg_bank_ = value;
        return;
    case 0x5200U:
        mode_ = value & 7U;
        return;
    case 0x5300U:
        speech_->write_byte(value);
        return;
    case 0x5500U:
        if (sb97_) {
            fdc_.configuration = value;
        }
        return;
    case 0x5501U:
        if (sb97_) {
            fdc_.output = value;
        }
        return;
    case 0x5505U:
        if (sb97_) {
            fdc_.data = value;
        }
        return;
    default:
        return;
    }
}

bool Mapper518::has_expansion_audio() const {
    return true;
}

void Mapper518::set_region(Region region) {
    region_ = region;
    speech_->set_region(region);
}

void Mapper518::clock_audio() {
    speech_->clock();
}

float Mapper518::audio_output() const {
    return static_cast<float>(speech_->output()) / 32768.0F * expansion_gains::kLpc;
}

void Mapper518::power_on() {
    reset();
    wram_.fill(0U);
    chr_ram_.fill(0U);
    std::ranges::fill(secondary_ram_, 0U);
}

void Mapper518::reset() {
    prg_bank_ = 0U;
    mode_ = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
    ciram_a10_ = 0U;
    ppu_a13_ = 0U;
    fdc_ = FdcRegisters{};
    speech_->reset();
}

std::uint16_t Mapper518::state_tag() const {
    return static_cast<std::uint16_t>(
        kStateTag | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) | (sb97_ ? 2U : 0U));
}

void Mapper518::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(prg_bank_);
    writer.write(mode_);
    writer.write(static_cast<Byte>(ciram_a10_ | (ppu_a13_ << 1U)));
    writer.write_array(wram_);
    writer.write_array(chr_ram_);
    writer.write_bytes(secondary_ram_);
    writer.write(fdc_.configuration);
    writer.write(fdc_.output);
    writer.write(fdc_.data);
    speech_->save_state(writer);
}

void Mapper518::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    const auto ppu_flags = reader.read<Byte>();
    std::array<Byte, kCartridgeRamSize> wram{};
    std::array<Byte, kCartridgeRamSize> chr_ram{};
    reader.read_array(wram);
    reader.read_array(chr_ram);
    std::vector<Byte> secondary_ram;
    reader.read_bytes(secondary_ram, kSecondaryRamSize);
    FdcRegisters fdc;
    reader.read(fdc.configuration);
    reader.read(fdc.output);
    reader.read(fdc.data);
    auto speech = *speech_;
    speech.load_state(reader);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        mode > 7U || ppu_flags > 3U || secondary_ram.size() != secondary_ram_.size()) {
        reader.fail();
        return;
    }

    prg_bank_ = prg_bank;
    mode_ = mode;
    ciram_a10_ = ppu_flags & 1U;
    ppu_a13_ = (ppu_flags >> 1U) & 1U;
    wram_ = wram;
    chr_ram_ = chr_ram;
    secondary_ram_ = std::move(secondary_ram);
    fdc_ = fdc;
    *speech_ = speech;
}

} // namespace mapperbus::core
