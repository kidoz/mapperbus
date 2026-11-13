// Standalone allocation probe; see docs/reference/memory-measurements.md.
// Counts requested bytes in C++ allocations, excluding malloc/GPU allocations.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/sdl3/gpu_upscaler.hpp"
#include "frontends/sdl3/sdl3_video.hpp"

namespace {
std::atomic<std::size_t> live_bytes{}, peak_bytes{}, largest_request{}, large_requests{};
std::atomic<bool> measuring{};
struct Allocation {
    void* base;
    std::size_t bytes;
};
void raise_max(std::atomic<std::size_t>& counter, std::size_t value) {
    auto old = counter.load();
    while (old < value && !counter.compare_exchange_weak(old, value)) {
    }
}
void* allocate(std::size_t bytes, std::size_t alignment) {
    const auto overhead = sizeof(Allocation) + alignment;
    if (bytes > std::numeric_limits<std::size_t>::max() - overhead)
        throw std::bad_alloc();
    void* base = std::malloc(bytes + overhead);
    if (!base)
        throw std::bad_alloc();
    const auto start = reinterpret_cast<std::uintptr_t>(base) + sizeof(Allocation);
    const auto aligned = (start + alignment - 1U) & ~(alignment - 1U);
    auto* record = reinterpret_cast<Allocation*>(aligned) - 1;
    *record = {base, bytes};
    const auto live = live_bytes.fetch_add(bytes) + bytes;
    if (measuring.load()) {
        raise_max(peak_bytes, live);
        raise_max(largest_request, bytes);
        if (bytes >= 1024U * 1024U)
            ++large_requests;
    }
    return reinterpret_cast<void*>(aligned);
}
void release(void* pointer) noexcept {
    if (!pointer)
        return;
    const auto* record = static_cast<Allocation*>(pointer) - 1;
    live_bytes.fetch_sub(record->bytes);
    std::free(record->base);
}
std::size_t begin_measurement() {
    const auto start = live_bytes.load();
    peak_bytes = start;
    largest_request = 0U;
    large_requests = 0U;
    measuring = true;
    return start;
}
void report(const char* mode, std::size_t start, bool gpu = false) {
    measuring = false;
    std::printf("{\"mode\":\"%s\",\"peak_cpp_bytes\":%zu,\"retained_cpp_bytes\":%zu,"
                "\"largest_request\":%zu,\"requests_at_least_1mib\":%zu,\"gpu_texture\":%s}\n",
                mode,
                peak_bytes.load() - start,
                live_bytes.load() - start,
                largest_request.load(),
                large_requests.load(),
                gpu ? "true" : "false");
}
} // namespace

// Interposition is confined to this probe executable. Production code is unchanged.
void* operator new(std::size_t bytes) {
    return allocate(bytes, alignof(std::max_align_t));
}
void* operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    return allocate(bytes, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) {
    return ::operator new(bytes, alignment);
}
void operator delete(void* pointer) noexcept {
    release(pointer);
}
void operator delete[](void* pointer) noexcept {
    release(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
    release(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    release(pointer);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
    release(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept {
    release(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    release(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    release(pointer);
}

int main(int argc, char** argv) {
    using namespace mapperbus;
    if (argc == 3 && std::string_view(argv[1]) == "rom") {
        core::register_builtin_mappers();
        const auto start = begin_measurement();
        auto cartridge = core::Cartridge::from_file(argv[2]);
        if (!cartridge) {
            measuring = false;
            std::fprintf(stderr, "%s\n", cartridge.error().c_str());
            return 1;
        }
        report("rom", start);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "gpu") {
        frontend::Sdl3Video video;
        auto upscaler = std::make_unique<frontend::GpuUpscaler>(6);
        auto* gpu = upscaler.get();
        video.set_upscaler(std::move(upscaler));
        auto frame = std::make_unique<core::FrameBuffer>();
        frame->pixels.fill(0xFF7193B5U);
        const auto start = begin_measurement();
        if (!video.initialize(core::kScreenWidth, core::kScreenHeight)) {
            measuring = false;
            std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
            return 1;
        }
        for (int i = 0; i < 3; ++i)
            video.render(*frame);
        const bool available = gpu->is_gpu_available() && gpu->get_gpu_texture() != nullptr;
        report("gpu", start, available);
        video.shutdown();
        SDL_Quit();
        return available ? 0 : 1;
    }
    std::fprintf(stderr, "Usage: %s rom IMAGE | gpu\n", argv[0]);
    return 2;
}
