#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/fds/fds.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

// Builds a synthetic FDS image of `sides` sides. Byte i of each side is a
// deterministic pattern so transfers can be checked against expected values.
[[nodiscard]] std::vector<Byte> make_fds_image(int sides, bool with_header) {
    std::vector<Byte> image;
    if (with_header) {
        image.insert(image.end(), {'F', 'D', 'S', 0x1A});
        image.resize(16, 0); // remaining header bytes
    }
    for (int s = 0; s < sides; ++s) {
        for (std::size_t i = 0; i < Fds::kSideSize; ++i) {
            image.push_back(static_cast<Byte>((i + static_cast<std::size_t>(s) * 7) & 0xFF));
        }
    }
    return image;
}

} // namespace

TEST_CASE("FDS loads a headerless single-side image", "[fds]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, /*with_header=*/false)));
    REQUIRE(fds.is_loaded());
    REQUIRE(fds.side_count() == 1);
    REQUIRE(fds.current_side() == 0);
}

TEST_CASE("FDS strips the fwNES header and counts sides", "[fds]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(2, /*with_header=*/true)));
    REQUIRE(fds.side_count() == 2);

    Fds empty;
    REQUIRE_FALSE(empty.load_disk(std::vector<Byte>{}));
}

TEST_CASE("FDS timer IRQ fires after the reload count", "[fds][irq]") {
    Fds fds;
    fds.write(0x4023, 0x01); // enable disk I/O registers
    fds.write(0x4020, 0x64); // reload low = 100
    fds.write(0x4021, 0x00); // reload high
    fds.write(0x4022, 0x02); // enable timer, one-shot

    fds.step(99);
    REQUIRE_FALSE(fds.irq_pending());
    fds.step(1);
    REQUIRE(fds.irq_pending());

    // Reading $4030 reports and acknowledges the timer IRQ.
    const Byte status = fds.read(0x4030);
    REQUIRE((status & 0x01) != 0);
    REQUIRE_FALSE(fds.irq_pending());

    // One-shot: it does not re-fire.
    fds.step(200);
    REQUIRE_FALSE(fds.irq_pending());
}

TEST_CASE("FDS timer IRQ repeats when configured", "[fds][irq]") {
    Fds fds;
    fds.write(0x4023, 0x01);
    fds.write(0x4020, 0x0A); // reload = 10
    fds.write(0x4021, 0x00);
    fds.write(0x4022, 0x03); // enable + repeat

    fds.step(10);
    REQUIRE(fds.irq_pending());
    fds.read(0x4030); // ack
    REQUIRE_FALSE(fds.irq_pending());
    fds.step(10);
    REQUIRE(fds.irq_pending()); // fired again
}

TEST_CASE("FDS streams disk bytes in order", "[fds][transfer]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    fds.write(0x4023, 0x01);               // enable disk I/O
    fds.write(0x4025, 0x01 | 0x04 | 0x40); // motor on + read mode + start

    // Spin-up, then the first byte appears.
    fds.step(200);
    REQUIRE((fds.read(0x4030) & 0x02) != 0); // byte-transfer flag set
    REQUIRE(fds.read(0x4031) == 0x00);       // side 0, byte 0

    // Subsequent bytes arrive one per byte-period.
    fds.step(149);
    REQUIRE(fds.read(0x4031) == 0x01);
    fds.step(149);
    REQUIRE(fds.read(0x4031) == 0x02);
}

TEST_CASE("FDS transfer can raise an IRQ per byte", "[fds][transfer][irq]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    fds.write(0x4023, 0x01);
    fds.write(0x4025, 0x01 | 0x04 | 0x40 | 0x80); // + IRQ on byte transfer

    fds.step(200);
    REQUIRE(fds.irq_pending());
    fds.read(0x4031); // reading the data clears the transfer IRQ
    REQUIRE_FALSE(fds.irq_pending());
}

TEST_CASE("FDS drive status reflects the inserted disk", "[fds]") {
    Fds fds;
    REQUIRE((fds.read(0x4032) & 0x01) != 0); // no disk: bit0 set

    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    REQUIRE((fds.read(0x4032) & 0x01) == 0); // disk present: bit0 clear
    REQUIRE((fds.read(0x4032) & 0x04) != 0); // write protected: bit2 set

    fds.eject();
    REQUIRE((fds.read(0x4032) & 0x01) != 0);
    REQUIRE(fds.current_side() == -1);
}

TEST_CASE("FDS state serializes drive + disk position", "[fds][state]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    fds.write(0x4023, 0x01);
    fds.write(0x4025, 0x01 | 0x04 | 0x40);
    fds.step(200 + 149 * 3); // advance a few bytes in

    StateWriter writer;
    fds.save_state(writer);

    Fds restored;
    StateReader reader(writer.data());
    restored.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(restored.is_loaded());
    REQUIRE(restored.current_side() == 0);

    // Both continue identically: next delivered byte matches.
    fds.step(149);
    restored.step(149);
    REQUIRE(restored.read(0x4031) == fds.read(0x4031));
}
