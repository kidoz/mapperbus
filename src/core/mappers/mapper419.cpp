#include "core/mappers/mapper419.hpp"

#include <bit>
#include <cmath>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x800000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x400000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kAudioBufferSize = 96UZ;
constexpr std::size_t kAudioFrameSize = 8UZ;
constexpr std::size_t kAudioFrameSamples = 21UZ;
constexpr std::uint16_t kStateTag = 0x4190U;
constexpr std::array<unsigned, 8> kPrgMasks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
constexpr std::array<unsigned, 8> kChrMasks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
constexpr std::array<Byte, 4> kIndexStep{0U, 0U, 3U, 5U};
constexpr std::array<Byte, 26> kIndexTable{0U,  0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,
                                           8U,  9U,  10U, 11U, 12U, 13U, 14U, 15U, 16U,
                                           17U, 18U, 19U, 20U, 20U, 20U, 20U, 20U};
constexpr std::array<std::array<int, 21>, 4> kStepTable{{
    {0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5, 5, 6, 7, 8, 10, 11, 13, 15},
    {1, 3, 3, 3, 4, 4, 6, 6, 7, 9, 10, 12, 15, 16, 19, 22, 25, 30, 34, 40, 46},
    {3, 5, 5, 6, 7, 8, 10, 11, 13, 16, 18, 21, 25, 28, 32, 38, 43, 51, 58, 68, 78},
    {4, 7, 7, 8, 10, 11, 14, 15, 18, 22, 25, 29, 35, 39, 45, 53, 60, 71, 81, 95, 109},
}};
constexpr std::array<Address, 6> kNormalNativePpu{
    0x1000U, 0x1400U, 0x1800U, 0x1C00U, 0x0000U, 0x0800U};
constexpr std::array<Address, 6> kNormalForwardedPpu{
    0x0000U, 0x0800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
constexpr std::array<Address, 6> kReversedForwardedPpu{
    0x1C00U, 0x1800U, 0x1400U, 0x1000U, 0x0800U, 0x0000U};

constexpr bool supported_submapper(Byte submapper) {
    return submapper <= 5U || (submapper >= 11U && submapper <= 15U);
}

} // namespace

// Original implementation from NESdev mapper 419 oldid 23377 and its Mapper 256
// submapper table. Opcode bytes need an instruction-fetch seam, not data-read
// transformation. The current console bus owns $4017, so status is exposed here
// for integration but cannot yet be merged into an actual controller read.
Mapper419::Mapper419(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), submapper_(header.submapper) {
    reset();
}

Result<void> Mapper419::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 419U) {
        return std::unexpected("Mapper 419 requires mapper ID 419");
    }
    if (!header.is_nes2 || !supported_submapper(header.submapper)) {
        return std::unexpected("Mapper 419 requires a documented NES 2.0 submapper");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        (chr_size != 0U &&
         (!std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize))) {
        return std::unexpected(
            "Mapper 419 requires power-of-two 32 KiB-8 MiB PRG and optional 8 KiB-4 MiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 419 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 419 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 419 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper419::canonical_chr_register(Address ppu_base) const {
    const auto slot = static_cast<unsigned>((ppu_base & 0x1FFFU) / kChrBankSize);
    return slot < 4U ? 4U + slot / 2U : slot - 4U;
}

Address Mapper419::native_ppu_base(unsigned raw_index) const {
    switch (submapper_) {
    case 1U: {
        constexpr std::array<Address, 6> bases{
            0x1400U, 0x1000U, 0x0800U, 0x0000U, 0x1C00U, 0x1800U};
        return bases[raw_index];
    }
    case 3U: {
        constexpr std::array<Address, 6> bases{
            0x0800U, 0x0000U, 0x1C00U, 0x1800U, 0x1000U, 0x1400U};
        return bases[raw_index];
    }
    case 4U: {
        constexpr std::array<Address, 6> bases{
            0x1800U, 0x0800U, 0x1000U, 0x0000U, 0x1C00U, 0x1400U};
        return bases[raw_index];
    }
    case 5U: {
        constexpr std::array<Address, 6> bases{
            0x1400U, 0x1000U, 0x0800U, 0x0000U, 0x0000U, 0x0800U};
        return bases[raw_index];
    }
    default:
        return kNormalNativePpu[raw_index];
    }
}

Address Mapper419::forwarded_ppu_base(unsigned bank_index) const {
    return submapper_ == 1U ? kReversedForwardedPpu[bank_index] : kNormalForwardedPpu[bank_index];
}

std::size_t Mapper419::prg_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    if ((registers_.control & 0x40U) != 0U && (slot & 1U) == 0U) {
        slot ^= 2U;
    }
    const unsigned inner = slot < 2U                                       ? registers_.prg[slot]
                           : slot == 2U && (registers_.mode & 0x40U) != 0U ? registers_.prg[2]
                           : slot == 2U                                    ? 254U
                                                                           : 255U;
    const unsigned mask = kPrgMasks[registers_.mode & 7U];
    const unsigned bank =
        (inner & mask) | (registers_.middle & (255U ^ mask)) | ((registers_.outer >> 4U) << 8U);
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t Mapper419::chr_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr & (kChrBankSize * 8U - 1U)) / kChrBankSize);
    if ((registers_.control & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const unsigned inner = slot < 4U ? (registers_.chr[4U + slot / 2U] & 254U) | (slot & 1U)
                                     : registers_.chr[slot - 4U];
    const unsigned mask = kChrMasks[registers_.chr_middle & 7U];
    const unsigned bank = (inner & mask) | (registers_.chr_middle & (255U ^ mask) & 248U) |
                          ((registers_.intermediate >> 4U) << 8U) |
                          ((registers_.outer & 15U) << 11U);
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
}

Byte Mapper419::read_prg(Address addr) {
    return maps_prg(addr) ? prg_rom_[prg_address(addr) & (prg_rom_.size() - 1U)] : 0U;
}

void Mapper419::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U || (registers_.mode & 8U) != 0U) {
        return;
    }
    if (submapper_ == 2U && (addr & 0x4000U) == 0U) {
        addr ^= 0x2000U;
    }
    switch (addr & 0xE001U) {
    case 0x8000U:
        write_system(5U, static_cast<Byte>((value & 0xDFU) | (registers_.control & 0x20U)));
        break;
    case 0x8001U: {
        const unsigned index = registers_.control & 7U;
        if (index < 6U) {
            registers_.chr[canonical_chr_register(forwarded_ppu_base(index))] = value;
        } else {
            registers_.prg[index - 6U] = value;
        }
        break;
    }
    case 0xA000U:
        write_system(6U, value);
        break;
    case 0xC000U:
        write_system(1U, value);
        break;
    case 0xC001U:
        write_system(2U, value);
        break;
    case 0xE000U:
        write_system(3U, value);
        break;
    case 0xE001U:
        write_system(4U, value);
        break;
    default:
        break;
    }
}

bool Mapper419::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper419::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper419::read_chr(Address addr) {
    const auto& storage = chr_rom_.empty() ? prg_rom_ : chr_rom_;
    return storage[chr_address(addr) & (storage.size() - 1U)];
}

void Mapper419::write_chr(Address, Byte) {}

MirrorMode Mapper419::mirror_mode() const {
    return registers_.mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Address> Mapper419::ciram_address(Address addr) const {
    return addr < 0x2000U && (registers_.control & 0x20U) != 0U
               ? std::optional<Address>(addr & 0x07FFU)
               : std::nullopt;
}

bool Mapper419::maps_expansion(Address addr) const {
    return (addr >= 0x4100U && addr <= 0x410BU) || addr == 0x410DU || addr == 0x410FU ||
           addr == 0x4017U ||
           (addr == 0x411CU && (submapper_ == 11U || submapper_ == 12U || submapper_ == 14U)) ||
           (addr == 0x4169U && (submapper_ == 13U || submapper_ == 15U));
}

Byte Mapper419::expansion_read_mask(Address addr) const {
    if (addr == 0x4017U) {
        return 0x18U;
    }
    return maps_expansion(addr) ? 0U : 0xFFU;
}

Byte Mapper419::read_expansion(Address addr) {
    if (addr != 0x4017U) {
        return 0U;
    }
    return static_cast<Byte>((audio_acknowledge_ ? 0x08U : 0U) |
                             (audio_ready_valid_ && audio_ready_ ? 0x10U : 0U));
}

void Mapper419::write_expansion(Address addr, Byte value) {
    if (addr >= 0x4100U && addr <= 0x410BU) {
        write_system(addr & 15U, value);
    } else if (addr == 0x410DU) {
        io_enabled_ = value == 0x30U;
        audio_acknowledge_ = !io_enabled_ || !io_clock_high_;
    } else if (addr == 0x410FU) {
        audio_nibble_ = value & 0x0FU;
    } else if (addr == 0x411CU && (submapper_ == 11U || submapper_ == 12U || submapper_ == 14U)) {
        opcode_control_ = value & (submapper_ == 11U ? 0x42U : 0x40U);
    } else if (addr == 0x4169U && (submapper_ == 13U || submapper_ == 15U)) {
        opcode_control_ = value & 1U;
    }
}

void Mapper419::observe_cpu_write(Address addr, Byte value) {
    if (addr != 0x4016U) {
        return;
    }
    const bool high = (value & 4U) != 0U;
    if (io_enabled_) {
        if (high && !io_clock_high_) {
            audio_high_nibble_ = static_cast<Byte>(audio_nibble_ << 4U);
            audio_acknowledge_ = false;
        } else if (!high && io_clock_high_) {
            receive_audio_byte(static_cast<Byte>(audio_high_nibble_ | audio_nibble_));
            audio_acknowledge_ = true;
        }
    } else {
        audio_acknowledge_ = true;
    }
    io_clock_high_ = high;
}

void Mapper419::write_system(unsigned reg, Byte value) {
    switch (reg) {
    case 0U:
        registers_.outer = value;
        break;
    case 1U:
        registers_.latch = value;
        break;
    case 2U:
        registers_.counter = 0U;
        break;
    case 3U:
        registers_.enabled = registers_.pending = false;
        break;
    case 4U:
        registers_.enabled = true;
        break;
    case 5U:
        registers_.control = value & 0xE7U;
        break;
    case 6U:
        registers_.mirror = value & 1U;
        break;
    case 7U:
    case 8U:
    case 9U:
        registers_.prg[reg - 7U] = value;
        break;
    case 10U:
        registers_.middle = value;
        break;
    case 11U:
        registers_.mode = value & 0xCFU;
        break;
    default:
        break;
    }
}

std::optional<Byte> Mapper419::read_ppu_register(Address addr, Byte open_bus) {
    return addr >= 0x2010U && addr <= 0x201FU ? std::optional<Byte>(open_bus) : std::nullopt;
}

bool Mapper419::write_ppu_register(Address addr, Byte value) {
    if (addr < 0x2010U || addr > 0x201FU) {
        return false;
    }
    if (addr >= 0x2012U && addr <= 0x2017U) {
        registers_.chr[canonical_chr_register(native_ppu_base(addr - 0x2012U))] = value;
    } else if (addr == 0x2018U) {
        registers_.intermediate = value & 0x70U;
    } else if (addr == 0x201AU) {
        registers_.chr_middle = value;
    }
    return true;
}

void Mapper419::reset() {
    registers_ = Registers{};
    opcode_control_ = submapper_ == 11U || submapper_ == 12U || submapper_ == 14U ? 0x40U : 0U;
    audio_command_ = 0U;
    audio_command_offset_ = 0U;
    period_low_ = 0U;
    reset_prefix_ = false;
    io_enabled_ = false;
    audio_nibble_ = 0U;
    audio_high_nibble_ = 0U;
    io_clock_high_ = false;
    audio_acknowledge_ = true;
    audio_ready_valid_ = false;
    audio_ready_ = false;
    playback_period_ = 0U;
    reset_audio_chip();
}

bool Mapper419::irq_pending() const {
    return registers_.pending;
}

bool Mapper419::observes_ppu_a12() const {
    return true;
}

void Mapper419::clock_counter() {
    registers_.counter = registers_.counter == 0U ? registers_.latch : registers_.counter - 1U;
    if (registers_.counter == 0U && registers_.enabled) {
        registers_.pending = true;
    }
}

void Mapper419::clock_cpu() {
    if (!registers_.a12 && registers_.low_cycles < 3U) {
        ++registers_.low_cycles;
    }
}

void Mapper419::observe_ppu_a12(bool high) {
    if (high && !registers_.a12 && registers_.low_cycles >= 3U && (registers_.mode & 0x80U) == 0U) {
        clock_counter();
    }
    registers_.a12 = high;
    if (high) {
        registers_.low_cycles = 0U;
    }
}

void Mapper419::on_ppu_hblank() {
    if ((registers_.mode & 0x80U) != 0U) {
        clock_counter();
    }
}

bool Mapper419::has_expansion_audio() const {
    return true;
}

std::uint64_t Mapper419::audio_threshold() const {
    return playback_period_ == 0U
               ? 0U
               : static_cast<std::uint64_t>(playback_period_) * cpu_clock_hz_ * 11U;
}

void Mapper419::set_region(Region region) {
    cpu_clock_hz_ =
        static_cast<std::uint32_t>(std::lround(timing_for_region(region).cpu_clock_hz()));
    const auto threshold = audio_threshold();
    audio_phase_ = threshold == 0U ? 0U : audio_phase_ % threshold;
}

void Mapper419::clock_audio() {
    const auto threshold = audio_threshold();
    if (threshold == 0U) {
        audio_phase_ = 0U;
        return;
    }
    audio_phase_ += 45000000U;
    while (audio_phase_ >= threshold) {
        audio_phase_ -= threshold;
        clock_sample();
    }
}

float Mapper419::audio_output() const {
    return static_cast<float>(output_) / 32768.0F;
}

void Mapper419::reset_decoder() {
    audio_buffer_.fill(0U);
    audio_read_ = 0U;
    audio_size_ = 0U;
    audio_frame_ = 0U;
    audio_frame_sample_ = 21U;
    predictor_ = 0;
    output_ = 0;
    audio_index_ = 0U;
    audio_phase_ = 0U;
}

void Mapper419::reset_audio_chip() {
    reset_decoder();
    audio_command_ = 0U;
    audio_command_offset_ = 0U;
    period_low_ = 0U;
    reset_prefix_ = false;
    playback_period_ = 0U;
    audio_ready_valid_ = false;
    audio_ready_ = false;
    audio_acknowledge_ = true;
}

void Mapper419::append_audio_byte(Byte value) {
    const auto write = static_cast<unsigned>((audio_read_ + audio_size_) % kAudioBufferSize);
    audio_buffer_[write] = value;
    ++audio_size_;
}

void Mapper419::receive_audio_byte(Byte value) {
    if (reset_prefix_ && value == 0xAAU) {
        reset_audio_chip();
        return;
    }
    reset_prefix_ = value == 0x55U;

    if (audio_command_ == 0x03U) {
        if (audio_command_offset_ == 0U) {
            period_low_ = value;
            audio_command_offset_ = 1U;
        } else {
            playback_period_ = static_cast<std::uint16_t>(period_low_ | (value << 8U));
            audio_phase_ = 0U;
            audio_command_ = 0U;
            audio_command_offset_ = 0U;
            audio_ready_valid_ = false;
            audio_ready_ = false;
        }
        return;
    }
    if (audio_command_ == 0x04U) {
        append_audio_byte(value);
        ++audio_command_offset_;
        if (audio_command_offset_ == kAudioBufferSize) {
            audio_command_ = 0U;
            audio_command_offset_ = 0U;
        }
        return;
    }
    if (audio_command_ == 0x06U) {
        append_audio_byte(value);
        ++audio_command_offset_;
        if (audio_command_offset_ == kAudioFrameSize) {
            audio_command_offset_ = 0U;
            audio_ready_valid_ = true;
            audio_ready_ = audio_size_ <= kAudioBufferSize - kAudioFrameSize;
            if (!audio_ready_) {
                audio_command_ = 0U;
            }
        }
        return;
    }

    audio_command_offset_ = 0U;
    audio_ready_valid_ = false;
    audio_ready_ = false;
    switch (value) {
    case 0x03U:
        audio_command_ = value;
        break;
    case 0x04U:
        reset_decoder();
        audio_command_ = value;
        break;
    case 0x06U:
        audio_ready_valid_ = true;
        audio_ready_ = audio_size_ <= kAudioBufferSize - kAudioFrameSize;
        if (audio_ready_) {
            audio_command_ = value;
        }
        break;
    case 0x07U:
        reset_decoder();
        break;
    default:
        break;
    }
}

void Mapper419::clock_sample() {
    if (audio_frame_sample_ == kAudioFrameSamples && audio_command_ != 0x04U &&
        audio_size_ >= kAudioFrameSize) {
        audio_frame_ = 0U;
        for (unsigned index = 0U; index < kAudioFrameSize; ++index) {
            audio_frame_ |= static_cast<std::uint64_t>(audio_buffer_[(audio_read_ + index) % 96U])
                            << (index * 8U);
        }
        audio_read_ = static_cast<Byte>((audio_read_ + kAudioFrameSize) % kAudioBufferSize);
        audio_size_ -= kAudioFrameSize;
        audio_frame_sample_ = 0U;
    }
    if (audio_frame_sample_ == kAudioFrameSamples) {
        return;
    }
    if ((audio_frame_ & 0x8000000000000000ULL) != 0U) {
        output_ = 0;
    } else {
        const auto code = static_cast<unsigned>((audio_frame_ >> (audio_frame_sample_ * 3U)) & 7U);
        const auto table = code & 3U;
        const int step = kStepTable[table][audio_index_];
        const int delta = (code & 4U) != 0U ? -step : step;
        predictor_ = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(predictor_) +
                                                 static_cast<std::uint32_t>(delta));
        audio_index_ = kIndexTable[audio_index_ + kIndexStep[table]];
        output_ = predictor_;
    }
    ++audio_frame_sample_;
}

std::uint16_t Mapper419::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | submapper_);
}

void Mapper419::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.outer);
    writer.write(registers_.control);
    writer.write(registers_.mirror);
    writer.write_array(registers_.prg);
    writer.write(registers_.middle);
    writer.write(registers_.mode);
    writer.write_array(registers_.chr);
    writer.write(registers_.intermediate);
    writer.write(registers_.chr_middle);
    writer.write(registers_.latch);
    writer.write(registers_.counter);
    writer.write<Byte>((registers_.enabled ? 1U : 0U) | (registers_.pending ? 2U : 0U));
    writer.write<Byte>((registers_.a12 ? 1U : 0U) | (registers_.low_cycles << 1U));
    writer.write(opcode_control_);
    writer.write_array(audio_buffer_);
    writer.write(audio_read_);
    writer.write(audio_size_);
    writer.write(audio_frame_);
    writer.write(audio_frame_sample_);
    writer.write(predictor_);
    writer.write(output_);
    writer.write(audio_index_);
    writer.write(playback_period_);
    writer.write(audio_phase_);
    writer.write(cpu_clock_hz_);
    writer.write(audio_command_);
    writer.write(audio_command_offset_);
    writer.write(period_low_);
    writer.write<Byte>((reset_prefix_ ? 1U : 0U) | (io_enabled_ ? 2U : 0U) |
                       (io_clock_high_ ? 4U : 0U) | (audio_acknowledge_ ? 8U : 0U) |
                       (audio_ready_valid_ ? 16U : 0U) | (audio_ready_ ? 32U : 0U));
    writer.write(audio_nibble_);
    writer.write(audio_high_nibble_);
}

void Mapper419::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.control);
    reader.read(next.mirror);
    reader.read_array(next.prg);
    reader.read(next.middle);
    reader.read(next.mode);
    reader.read_array(next.chr);
    reader.read(next.intermediate);
    reader.read(next.chr_middle);
    reader.read(next.latch);
    reader.read(next.counter);
    const auto irq = reader.read<Byte>();
    const auto pins = reader.read<Byte>();
    const auto opcode_control = reader.read<Byte>();
    std::array<Byte, kAudioBufferSize> audio_buffer{};
    reader.read_array(audio_buffer);
    const auto audio_read = reader.read<Byte>();
    const auto audio_size = reader.read<Byte>();
    const auto audio_frame = reader.read<std::uint64_t>();
    const auto audio_frame_sample = reader.read<Byte>();
    const auto predictor = reader.read<std::int32_t>();
    const auto output = reader.read<std::int32_t>();
    const auto audio_index = reader.read<Byte>();
    const auto playback_period = reader.read<std::uint16_t>();
    const auto audio_phase = reader.read<std::uint64_t>();
    const auto cpu_clock_hz = reader.read<std::uint32_t>();
    const auto audio_command = reader.read<Byte>();
    const auto audio_command_offset = reader.read<Byte>();
    const auto period_low = reader.read<Byte>();
    const auto audio_flags = reader.read<Byte>();
    const auto audio_nibble = reader.read<Byte>();
    const auto audio_high_nibble = reader.read<Byte>();

    const auto threshold = playback_period == 0U
                               ? 0U
                               : static_cast<std::uint64_t>(playback_period) * cpu_clock_hz * 11U;
    const bool valid_cpu_clock =
        cpu_clock_hz == static_cast<std::uint32_t>(std::lround(kNtscTiming.cpu_clock_hz())) ||
        cpu_clock_hz == static_cast<std::uint32_t>(std::lround(kPalTiming.cpu_clock_hz())) ||
        cpu_clock_hz == static_cast<std::uint32_t>(std::lround(kDendyTiming.cpu_clock_hz()));
    const bool valid_command = (audio_command == 0U && audio_command_offset == 0U) ||
                               (audio_command == 0x03U && audio_command_offset <= 1U) ||
                               (audio_command == 0x04U && audio_command_offset < kAudioBufferSize &&
                                audio_command_offset == audio_size && audio_read == 0U) ||
                               (audio_command == 0x06U && audio_command_offset < kAudioFrameSize &&
                                (audio_flags & 0x30U) == 0x30U);
    const Byte opcode_mask = submapper_ == 11U                        ? 0x42U
                             : submapper_ == 12U || submapper_ == 14U ? 0x40U
                             : submapper_ == 13U || submapper_ == 15U ? 1U
                                                                      : 0U;
    const bool valid =
        reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
        chr_size == chr_rom_.size() && ram_size == 0U && (next.control & ~0xE7U) == 0U &&
        next.mirror <= 1U && (next.mode & ~0xCFU) == 0U && (next.intermediate & ~0x70U) == 0U &&
        irq <= 3U && irq != 2U && pins <= 6U && ((pins & 1U) == 0U || pins == 1U) &&
        (opcode_control & ~opcode_mask) == 0U && audio_read < kAudioBufferSize &&
        audio_read % kAudioFrameSize == 0U && audio_size <= kAudioBufferSize &&
        audio_frame_sample <= kAudioFrameSamples && audio_index <= 20U && valid_cpu_clock &&
        ((threshold == 0U && audio_phase == 0U) || (threshold != 0U && audio_phase < threshold)) &&
        valid_command && audio_flags <= 0x3FU && audio_nibble <= 0x0FU &&
        audio_high_nibble <= 0xF0U && (audio_high_nibble & 0x0FU) == 0U &&
        ((audio_flags & 0x20U) == 0U || (audio_flags & 0x10U) != 0U);
    if (!valid) {
        reader.fail();
        return;
    }

    next.enabled = (irq & 1U) != 0U;
    next.pending = (irq & 2U) != 0U;
    next.a12 = (pins & 1U) != 0U;
    next.low_cycles = pins >> 1U;
    registers_ = next;
    opcode_control_ = opcode_control;
    audio_buffer_ = audio_buffer;
    audio_read_ = audio_read;
    audio_size_ = audio_size;
    audio_frame_ = audio_frame;
    audio_frame_sample_ = audio_frame_sample;
    predictor_ = predictor;
    output_ = output;
    audio_index_ = audio_index;
    playback_period_ = playback_period;
    audio_phase_ = audio_phase;
    cpu_clock_hz_ = cpu_clock_hz;
    audio_command_ = audio_command;
    audio_command_offset_ = audio_command_offset;
    period_low_ = period_low;
    reset_prefix_ = (audio_flags & 1U) != 0U;
    io_enabled_ = (audio_flags & 2U) != 0U;
    io_clock_high_ = (audio_flags & 4U) != 0U;
    audio_acknowledge_ = (audio_flags & 8U) != 0U;
    audio_ready_valid_ = (audio_flags & 16U) != 0U;
    audio_ready_ = (audio_flags & 32U) != 0U;
    audio_nibble_ = audio_nibble;
    audio_high_nibble_ = audio_high_nibble;
}

} // namespace mapperbus::core
