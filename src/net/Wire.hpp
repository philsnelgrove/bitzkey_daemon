#pragma once
// The binary form in which a main service and its workers pass brains, senses and values: little-endian whole
// numbers and 32-bit floats, written and read in order. Floats are enough: a brain on a GPU thinks in them anyway.

#include <bit>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/Brain.hpp"

namespace bitzkey::net::wire {

static_assert(std::endian::native == std::endian::little, "the wire format is written as the machine holds it: little-endian");

class Writer {
public:
    template <typename T>
    void put(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto at = bytes_.size();
        bytes_.resize(at + sizeof(T));
        std::memcpy(bytes_.data() + at, &value, sizeof(T));
    }
    void floats(const double* values, std::size_t count) {
        const auto at = bytes_.size();
        bytes_.resize(at + count * sizeof(float));
        char* into = bytes_.data() + at;
        for (std::size_t index = 0; index < count; ++index, into += sizeof(float)) {
            const float value = static_cast<float>(values[index]);
            std::memcpy(into, &value, sizeof(float));
        }
    }
    void raw(const void* data, std::size_t size) { bytes_.append(static_cast<const char*>(data), size); }
    void brain(const Brain& brain) {
        put<std::uint8_t>(static_cast<std::uint8_t>(brain.depth()));
        for (const Layer& layer : brain.layers) {
            put<std::uint16_t>(static_cast<std::uint16_t>(layer.reads));
            floats(layer.weights.data(), layer.weights.size());
            floats(layer.biases.data(), layer.biases.size());
        }
        for (const auto& row : brain.output_weights) floats(row.data(), row.size());
        floats(brain.output_biases.data(), brain.output_biases.size());
    }
    std::string& bytes() { return bytes_; }

private:
    std::string bytes_;
};

class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}
    template <typename T>
    T get() {
        static_assert(std::is_trivially_copyable_v<T>);
        need(sizeof(T));
        T value;
        std::memcpy(&value, bytes_.data() + at_, sizeof(T));
        at_ += sizeof(T);
        return value;
    }
    void floats(double* values, std::size_t count) {
        need(count * sizeof(float));
        for (std::size_t index = 0; index < count; ++index, at_ += sizeof(float)) {
            float value;
            std::memcpy(&value, bytes_.data() + at_, sizeof(float));
            values[index] = value;
        }
    }
    void raw(void* into, std::size_t size) {
        need(size);
        std::memcpy(into, bytes_.data() + at_, size);
        at_ += size;
    }
    Brain brain() {
        Brain made;
        const int depth = get<std::uint8_t>();
        if (depth < 1 || depth > 64) throw std::runtime_error("a brain of no sensible depth");
        made.layers.resize(static_cast<std::size_t>(depth));
        for (int index = 0; index < depth; ++index) {
            Layer& layer = made.layers[static_cast<std::size_t>(index)];
            layer.reads = get<std::uint16_t>();
            if (layer.reads != (index ? HIDDEN_SIZE : INPUT_SIZE)) throw std::runtime_error("a brain of another shape");
            layer.weights.resize(static_cast<std::size_t>(HIDDEN_SIZE) * static_cast<std::size_t>(layer.reads));
            floats(layer.weights.data(), layer.weights.size());
            floats(layer.biases.data(), layer.biases.size());
        }
        for (auto& row : made.output_weights) floats(row.data(), row.size());
        floats(made.output_biases.data(), made.output_biases.size());
        return made;
    }
    bool done() const { return at_ == bytes_.size(); }

private:
    void need(std::size_t size) const {
        if (bytes_.size() - at_ < size) throw std::runtime_error("a message cut short");
    }
    std::string_view bytes_;
    std::size_t at_ = 0;
};

}  // namespace bitzkey::net::wire
