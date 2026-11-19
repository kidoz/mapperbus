#include "core/mappers/mapper284_drip.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::uint32_t kStateTag = 0x44524950U;
constexpr float kPcmGain = 1.0F / 2048.0F;
} // namespace

Drip::Drip(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), battery_(header.has_battery) {
    power_on();
}

Result<void> Drip::validate_geometry(const INesHeader& header,
                                     std::size_t prg_size,
                                     std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U) {
        return std::unexpected("Mapper 284 supports submapper 0");
    }
    if (prg_size < 16384U || prg_size > 262144U || prg_size % 16384U != 0U || chr_size < 2048U ||
        chr_size > 32768U || chr_size % 2048U != 0U) {
        return std::unexpected(
            "Mapper 284 requires 16-256 KiB PRG and 2-32 KiB CHR in addressable banks");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 284 requires ordinary H/V nametable metadata");
    }
    if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != (header.has_battery ? 0U : 8192U) ||
                            header.prg_nvram_size != (header.has_battery ? 8192U : 0U)))) {
        return std::unexpected("Mapper 284 requires exactly 8 KiB PRG RAM and no CHR RAM");
    }
    return {};
}

std::span<Byte> Drip::trainer_ram() {
    return std::span{wram_}.subspan(0x1000U);
}

bool Drip::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Drip::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return addr >= 0x6000U ? wram_[addr & 0x1FFFU] : 0U;
    }
    if (addr < 0xC000U) {
        const std::size_t offset = static_cast<std::size_t>(prg_bank_) * 16384U + (addr & 0x3FFFU);
        return prg_rom_[offset % prg_rom_.size()];
    }
    return prg_rom_[prg_rom_.size() - 16384U + (addr & 0x3FFFU)];
}

void Drip::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((control_ & 0x04U) != 0U) {
            wram_[addr & 0x1FFFU] = value;
        }
        return;
    }
    if (addr >= 0xC000U) {
        const std::size_t table = (addr >> 10U) & 1U;
        extended_attributes_[table * 1024U + (addr & 0x03FFU)] = value & 0x03U;
        return;
    }
    if (addr >= 0x8000U) {
        write_register(addr & 0x0FU, value);
    }
}

Byte Drip::read_chr(Address addr) {
    const std::size_t bank = chr_banks_[(addr >> 11U) & 3U];
    const std::size_t offset = bank * 2048U + (addr & 0x07FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Drip::write_chr(Address, Byte) {}

MirrorMode Drip::mirror_mode() const {
    return mirror_;
}

bool Drip::maps_expansion(Address addr) const {
    return addr >= 0x4800U && addr <= 0x5FFFU;
}

Byte Drip::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0xFFU : 0U;
}

Byte Drip::read_expansion(Address addr) {
    if (addr >= 0x4800U && addr <= 0x4FFFU) {
        return static_cast<Byte>(0x64U | (dip_switch_ << 7U));
    }
    if (addr >= 0x5000U && addr <= 0x57FFU) {
        const auto& channel = channels_[0];
        return static_cast<Byte>((channel.count == 256U ? 0x80U : 0U) |
                                 (channel.count == 0U ? 0x40U : 0U));
    }
    if (addr >= 0x5800U && addr <= 0x5FFFU) {
        const auto& channel = channels_[1];
        return static_cast<Byte>((channel.count == 256U ? 0x80U : 0U) |
                                 (channel.count == 0U ? 0x40U : 0U));
    }
    return 0U;
}

Result<void> Drip::configure_board_jumpers(Byte value) {
    if (value > 1U) {
        return std::unexpected("Mapper 284 DIP switch must be 0 or 1");
    }
    dip_switch_ = value;
    return {};
}

void Drip::write_register(unsigned reg, Byte value) {
    const unsigned audio_reg = reg & 0x03U;
    if (reg < 8U) {
        auto& channel = channels_[reg >= 4U ? 1U : 0U];
        switch (audio_reg) {
        case 0U:
            clear_channel(channel);
            break;
        case 1U:
            push_sample(channel, value);
            break;
        case 2U:
            channel.period = (channel.period & 0x0F00U) | value;
            break;
        default:
            channel.period =
                (channel.period & 0x00FFU) | (static_cast<std::uint16_t>(value & 0x0FU) << 8U);
            channel.volume = value >> 4U;
            break;
        }
        return;
    }
    switch (reg) {
    case 8U:
        irq_low_ = value;
        break;
    case 9U:
        irq_counter_ = (static_cast<std::uint16_t>(value & 0x7FU) << 8U) | irq_low_;
        irq_enabled_ = (value & 0x80U) != 0U;
        irq_pending_ = false;
        break;
    case 10U:
        control_ = value & 0x0FU;
        switch (control_ & 0x03U) {
        case 0U:
            mirror_ = MirrorMode::Vertical;
            break;
        case 1U:
            mirror_ = MirrorMode::Horizontal;
            break;
        case 2U:
            mirror_ = MirrorMode::SingleLower;
            break;
        default:
            mirror_ = MirrorMode::SingleUpper;
            break;
        }
        break;
    case 11U:
        prg_bank_ = value & 0x0FU;
        break;
    default:
        chr_banks_[reg - 12U] = value & 0x0FU;
        break;
    }
}

void Drip::power_on() {
    reset();
    if (!battery_) {
        wram_.fill(0U);
    }
    extended_attributes_.fill(0U);
}

void Drip::reset() {
    control_ = 0U;
    mirror_ = MirrorMode::Vertical;
    prg_bank_ = 0U;
    chr_banks_.fill(0U);
    irq_low_ = 0U;
    irq_counter_ = 0U;
    irq_enabled_ = false;
    irq_pending_ = false;
    for (auto& channel : channels_) {
        channel = {};
    }
}

std::optional<Byte> Drip::background_attribute_palette(Address tile_address) const {
    if ((control_ & 0x08U) == 0U || tile_address < 0x2000U || tile_address >= 0x3000U) {
        return std::nullopt;
    }
    const auto offset = static_cast<std::size_t>(tile_address & 0x03FFU);
    const unsigned logical_table = (tile_address >> 10U) & 3U;
    unsigned physical_table = 0U;
    switch (mirror_) {
    case MirrorMode::Horizontal:
        physical_table = logical_table < 2U ? 0U : 1U;
        break;
    case MirrorMode::Vertical:
        physical_table = logical_table & 1U;
        break;
    case MirrorMode::SingleUpper:
        physical_table = 1U;
        break;
    default:
        physical_table = 0U;
        break;
    }
    return extended_attributes_[physical_table * 1024U + offset];
}

bool Drip::irq_pending() const {
    return irq_pending_;
}

void Drip::acknowledge_irq() {
    irq_pending_ = false;
}

void Drip::clock_cpu() {
    if (!irq_enabled_) {
        return;
    }
    if (irq_counter_ != 0U) {
        --irq_counter_;
    }
    if (irq_counter_ == 0U) {
        irq_enabled_ = false;
        irq_pending_ = true;
    }
}

void Drip::clear_channel(PcmChannel& channel) {
    channel.read_index = 0U;
    channel.count = 0U;
    channel.active_period = channel.period;
    channel.timer = channel.period;
    channel.pwm_phase = 0U;
    channel.active = false;
}

void Drip::push_sample(PcmChannel& channel, Byte value) {
    if (channel.count == 256U) {
        return;
    }
    const bool start = !channel.active && channel.count == 0U;
    const auto write_index = static_cast<Byte>(channel.read_index + channel.count);
    channel.fifo[write_index] = value;
    ++channel.count;
    if (start) {
        channel.active_period = channel.period;
        channel.timer = channel.period;
        channel.pwm_phase = 0U;
        channel.active = true;
    }
}

void Drip::clock_channel(PcmChannel& channel) {
    if (!channel.active) {
        return;
    }
    if (channel.timer > 1U) {
        --channel.timer;
        channel.pwm_phase = (channel.pwm_phase + 1U) & 0x0FU;
        return;
    }
    channel.read_index = static_cast<Byte>(channel.read_index + 1U);
    --channel.count;
    channel.pwm_phase = 0U;
    if (channel.count == 0U) {
        channel.active = false;
        return;
    }
    channel.active_period = channel.period;
    channel.timer = channel.period;
}

bool Drip::has_expansion_audio() const {
    return true;
}

void Drip::clock_audio() {
    for (auto& channel : channels_) {
        clock_channel(channel);
    }
}

float Drip::channel_output(const PcmChannel& channel) const {
    if (!channel.active || channel.pwm_phase > channel.volume) {
        return 0.0F;
    }
    const int sample = static_cast<int>(channel.fifo[channel.read_index]) - 128;
    return static_cast<float>(sample) * kPcmGain;
}

float Drip::audio_output() const {
    return channel_output(channels_[0]) + channel_output(channels_[1]);
}

std::span<const Byte> Drip::battery_ram() const {
    return battery_ ? std::span<const Byte>{wram_} : std::span<const Byte>{};
}

void Drip::set_battery_ram(std::span<const Byte> data) {
    if (battery_ && data.size() == wram_.size()) {
        std::ranges::copy(data, wram_.begin());
    }
}

std::uint32_t Drip::state_tag() const {
    return kStateTag;
}

void Drip::save_channel(StateWriter& writer, const PcmChannel& channel) const {
    writer.write_array(channel.fifo);
    writer.write(channel.read_index);
    writer.write(channel.count);
    writer.write(channel.period);
    writer.write(channel.active_period);
    writer.write(channel.timer);
    writer.write(channel.volume);
    writer.write(channel.pwm_phase);
    writer.write(static_cast<Byte>(channel.active));
}

bool Drip::load_channel(StateReader& reader, PcmChannel& channel) const {
    reader.read_array(channel.fifo);
    channel.read_index = reader.read<Byte>();
    channel.count = reader.read<std::uint16_t>();
    channel.period = reader.read<std::uint16_t>();
    channel.active_period = reader.read<std::uint16_t>();
    channel.timer = reader.read<std::uint16_t>();
    channel.volume = reader.read<Byte>();
    channel.pwm_phase = reader.read<Byte>();
    const auto active = reader.read<Byte>();
    channel.active = active != 0U;
    return active <= 1U;
}

void Drip::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<Byte>((battery_ ? 1U : 0U) | (dip_switch_ << 1U)));
    writer.write(control_);
    writer.write(prg_bank_);
    writer.write_array(chr_banks_);
    writer.write(irq_low_);
    writer.write(irq_counter_);
    writer.write(static_cast<Byte>((irq_enabled_ ? 1U : 0U) | (irq_pending_ ? 2U : 0U)));
    writer.write_array(wram_);
    writer.write_array(extended_attributes_);
    for (const auto& channel : channels_) {
        save_channel(writer, channel);
    }
}

void Drip::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint32_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto config = reader.read<Byte>();
    const auto control = reader.read<Byte>();
    const auto prg_bank = reader.read<Byte>();
    std::array<Byte, 4> chr_banks{};
    reader.read_array(chr_banks);
    const auto irq_low = reader.read<Byte>();
    const auto irq_counter = reader.read<std::uint16_t>();
    const auto irq_flags = reader.read<Byte>();
    std::array<Byte, 8192> wram{};
    std::array<Byte, 2048> attributes{};
    reader.read_array(wram);
    reader.read_array(attributes);
    std::array<PcmChannel, 2> channels{};
    bool valid_channels = true;
    for (auto& channel : channels) {
        valid_channels = load_channel(reader, channel) && valid_channels;
    }

    bool valid = valid_channels && reader.ok() && tag == state_tag() &&
                 prg_size == prg_rom_.size() && chr_size == chr_rom_.size() &&
                 config == static_cast<Byte>((battery_ ? 1U : 0U) | (dip_switch_ << 1U)) &&
                 control <= 0x0FU && prg_bank <= 0x0FU && irq_counter <= 0x7FFFU &&
                 irq_flags <= 0x03U && ((irq_flags & 0x03U) != 0x03U) &&
                 std::ranges::all_of(chr_banks, [](Byte bank) { return bank <= 0x0FU; }) &&
                 std::ranges::all_of(attributes, [](Byte value) { return value <= 0x03U; });
    for (const auto& channel : channels) {
        valid = valid && channel.count <= 256U && channel.period <= 0x0FFFU &&
                channel.active_period <= 0x0FFFU && channel.timer <= 0x0FFFU &&
                channel.volume <= 0x0FU && channel.pwm_phase <= 0x0FU &&
                channel.active == (channel.count != 0U) &&
                (!channel.active || channel.timer <= channel.active_period);
    }
    if (!valid) {
        reader.fail();
        return;
    }

    control_ = control;
    switch (control & 0x03U) {
    case 0U:
        mirror_ = MirrorMode::Vertical;
        break;
    case 1U:
        mirror_ = MirrorMode::Horizontal;
        break;
    case 2U:
        mirror_ = MirrorMode::SingleLower;
        break;
    default:
        mirror_ = MirrorMode::SingleUpper;
        break;
    }
    prg_bank_ = prg_bank;
    chr_banks_ = chr_banks;
    irq_low_ = irq_low;
    irq_counter_ = irq_counter;
    irq_enabled_ = (irq_flags & 0x01U) != 0U;
    irq_pending_ = (irq_flags & 0x02U) != 0U;
    wram_ = wram;
    extended_attributes_ = attributes;
    channels_ = channels;
}

} // namespace mapperbus::core
