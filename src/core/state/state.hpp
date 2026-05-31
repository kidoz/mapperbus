#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::core {

/// Append-only binary state serializer.
///
/// State blobs are host-endian and host-layout: they are intended for
/// save-states and rewind on the same machine/build, not as a portable
/// archive format. Versioning is handled at the Emulator level.
class StateWriter {
  public:
    template <typename T> void write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "StateWriter::write requires a trivially copyable type");
        const auto* bytes = reinterpret_cast<const Byte*>(&value);
        buffer_.insert(buffer_.end(), bytes, bytes + sizeof(T));
    }

    template <typename T, std::size_t N> void write_array(const std::array<T, N>& values) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "StateWriter::write_array requires a trivially copyable element type");
        const auto* bytes = reinterpret_cast<const Byte*>(values.data());
        buffer_.insert(buffer_.end(), bytes, bytes + sizeof(T) * N);
    }

    /// Length-prefixed byte run (for variable-size buffers such as PRG/CHR RAM).
    void write_bytes(std::span<const Byte> data) {
        write<std::uint64_t>(data.size());
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    [[nodiscard]] const std::vector<Byte>& data() const {
        return buffer_;
    }
    [[nodiscard]] std::vector<Byte> take() {
        return std::move(buffer_);
    }

  private:
    std::vector<Byte> buffer_;
};

/// Sequential reader paired with StateWriter. Bounds are checked; any
/// short/garbled read latches `ok()` to false so callers can reject the blob.
class StateReader {
  public:
    explicit StateReader(std::span<const Byte> data) : data_(data) {}

    template <typename T> void read(T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "StateReader::read requires a trivially copyable type");
        if (!ok_ || pos_ + sizeof(T) > data_.size()) {
            ok_ = false;
            value = T{};
            return;
        }
        std::memcpy(&value, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
    }

    template <typename T> [[nodiscard]] T read() {
        T value{};
        read(value);
        return value;
    }

    template <typename T, std::size_t N> void read_array(std::array<T, N>& values) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "StateReader::read_array requires a trivially copyable element type");
        if (!ok_ || pos_ + sizeof(T) * N > data_.size()) {
            ok_ = false;
            values = {};
            return;
        }
        std::memcpy(values.data(), data_.data() + pos_, sizeof(T) * N);
        pos_ += sizeof(T) * N;
    }

    /// Reads a length-prefixed byte run written by write_bytes.
    void read_bytes(std::vector<Byte>& out) {
        const auto size = read<std::uint64_t>();
        if (!ok_ || pos_ + size > data_.size()) {
            ok_ = false;
            out.clear();
            return;
        }
        out.assign(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                   data_.begin() + static_cast<std::ptrdiff_t>(pos_ + size));
        pos_ += size;
    }

    [[nodiscard]] bool ok() const {
        return ok_;
    }
    [[nodiscard]] std::size_t remaining() const {
        return data_.size() - pos_;
    }

  private:
    std::span<const Byte> data_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

/// Marker interface for components that participate in save/load state.
class Serializable {
  public:
    virtual ~Serializable() = default;
    virtual void save_state(StateWriter&) const {}
    virtual void load_state(StateReader&) {}
};

} // namespace mapperbus::core
