#include <catch2/catch_test_macros.hpp>

#include "core/apu/apu.hpp"

using namespace mapperbus::core;

TEST_CASE("RingBuffer push and pop", "[ring-buffer]") {
    RingBuffer<float> rb(16);
    REQUIRE(rb.available() == 0);

    REQUIRE(rb.try_push(1.0f));
    REQUIRE(rb.try_push(2.0f));
    REQUIRE(rb.try_push(3.0f));
    REQUIRE(rb.available() == 3);

    float out[3];
    size_t n = rb.read(out, 3);
    REQUIRE(n == 3);
    REQUIRE(out[0] == 1.0f);
    REQUIRE(out[1] == 2.0f);
    REQUIRE(out[2] == 3.0f);
    REQUIRE(rb.available() == 0);
}

TEST_CASE("RingBuffer wraparound", "[ring-buffer]") {
    RingBuffer<int> rb(4);
    for (int round = 0; round < 5; ++round) {
        REQUIRE(rb.try_push(round * 10 + 1));
        REQUIRE(rb.try_push(round * 10 + 2));
        REQUIRE(rb.try_push(round * 10 + 3));

        int out[3];
        size_t n = rb.read(out, 3);
        REQUIRE(n == 3);
        REQUIRE(out[0] == round * 10 + 1);
        REQUIRE(out[1] == round * 10 + 2);
        REQUIRE(out[2] == round * 10 + 3);
    }
}

TEST_CASE("RingBuffer full returns false", "[ring-buffer]") {
    RingBuffer<int> rb(4);
    REQUIRE(rb.try_push(1));
    REQUIRE(rb.try_push(2));
    REQUIRE(rb.try_push(3));
    REQUIRE(rb.try_push(4));
    REQUIRE_FALSE(rb.try_push(5));

    int out = 0;
    REQUIRE(rb.read(&out, 1) == 1);
    REQUIRE(out == 1);
    REQUIRE(rb.try_push(5));
}

TEST_CASE("RingBuffer empty read returns zero", "[ring-buffer]") {
    RingBuffer<float> rb(8);
    float out = 0;
    REQUIRE(rb.read(&out, 1) == 0);
}

TEST_CASE("RingBuffer batch read", "[ring-buffer]") {
    RingBuffer<float> rb(16);
    for (int i = 0; i < 10; ++i) {
        rb.try_push(static_cast<float>(i));
    }

    float out[16];
    size_t n = rb.read(out, 16);
    REQUIRE(n == 10);
    for (int i = 0; i < 10; ++i) {
        REQUIRE(out[i] == static_cast<float>(i));
    }
}

TEST_CASE("RingBuffer reset", "[ring-buffer]") {
    RingBuffer<int> rb(8);
    rb.try_push(1);
    rb.try_push(2);
    rb.try_push(3);
    REQUIRE(rb.available() == 3);

    rb.reset();
    REQUIRE(rb.available() == 0);

    int out = 0;
    REQUIRE(rb.read(&out, 1) == 0);
}
