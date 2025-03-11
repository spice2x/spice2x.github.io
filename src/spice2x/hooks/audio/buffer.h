#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <limits>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

enum class SampleType {
    UNSUPPORTED = 0,
    SINT_16,
    SINT_24,
    SINT_32,
    FLOAT_32,
    FLOAT_64,
};

static constexpr const char *sample_type_str(SampleType sample_type) {
    switch (sample_type) {
        case SampleType::UNSUPPORTED:
            return "UNSUPPORTED";
        case SampleType::SINT_16:
            return "SINT_16";
        case SampleType::SINT_24:
            return "SINT_24";
        case SampleType::SINT_32:
            return "SINT_32";
        case SampleType::FLOAT_32:
            return "FLOAT_32";
        case SampleType::FLOAT_64:
            return "FLOAT_64";
        default:
            return "Unknown";
    }
}

struct int24_t {
    uint8_t c3[3] {};

    constexpr int24_t() = default;

    constexpr int24_t(const short &i) noexcept {
        *this = static_cast<int>(i);
    }
    constexpr int24_t(const int &i) noexcept {
        *this = i;
    }
    constexpr int24_t(const long &l) noexcept {
        *this = static_cast<int>(l);
    }
    constexpr int24_t(const float &f) noexcept {
        *this = static_cast<int>(f);
    }
    constexpr int24_t(const double &d) noexcept {
        *this = static_cast<int>(d);
    }

    constexpr int24_t &operator=(const int &i) noexcept {
        c3[0] = (i & 0x0000ffu);
        c3[1] = (i & 0x00ff00u) >> 8;
        c3[2] = (i & 0xff0000u) >> 16;

        return *this;
    }

    constexpr int32_t as_int() noexcept {
        uint32_t i = c3[0] | (c3[1] << 8) | (c3[2] << 16);
        if (i & 0x800000) {
            i |= ~0xffffffu;
        }
        return static_cast<int32_t>(i);
    }
};

template<>
class std::numeric_limits<int24_t> {
public:
    static constexpr int32_t(min)() noexcept {
        return -0x800000;
    }
    static constexpr int32_t(max)() noexcept {
        return 0x7fffff;
    }
};

template<typename T>
class conversion_limits {
public:
    static constexpr double absolute_max_value() noexcept {

        // 1.0 is added here because the minimum value is `abs(min)` because of two's complement
        return static_cast<double>(std::numeric_limits<T>::max()) + 1.0;
    }
};

static_assert(std::numeric_limits<int16_t>::max() == 32767);
static_assert(std::numeric_limits<int24_t>::max() == 8388607);
static_assert(std::numeric_limits<int32_t>::max() == 2147483647);
static_assert(std::numeric_limits<int64_t>::max() == 9223372036854775807LL);
static_assert(conversion_limits<int16_t>::absolute_max_value() == 32768.0);
static_assert(conversion_limits<int24_t>::absolute_max_value() == 8388608.0);
static_assert(conversion_limits<int32_t>::absolute_max_value() == 2147483648.0);
static_assert(conversion_limits<int64_t>::absolute_max_value() == 9223372036854775808.0);

template<typename T, typename U = T>
constexpr double convert_number_to_double(T num) {
    return static_cast<double>(num) / conversion_limits<U>::absolute_max_value();
}

template<typename T>
constexpr T convert_double_to_number(double num) {
    constexpr auto ABSOLUTE_MAX_VALUE = conversion_limits<T>::absolute_max_value();
    constexpr auto MAX_VALUE = static_cast<long>(std::numeric_limits<T>::max());

    return static_cast<T>(std::min(std::lround(num * ABSOLUTE_MAX_VALUE), MAX_VALUE));
}

// ...before Felix makes this mistake again, make sure 24-bit ints are converted with the correct range
static_assert(convert_number_to_double<int32_t, int24_t>(8388607) == (8388607.0 / 8388608.0));

static constexpr size_t sample_type_size(SampleType sample_type) {
    switch (sample_type) {
        case SampleType::UNSUPPORTED:
            return 0;
        case SampleType::SINT_16:
            return sizeof(int16_t);
        case SampleType::SINT_24:
            return sizeof(int24_t);
        case SampleType::SINT_32:
            return sizeof(int32_t);
        case SampleType::FLOAT_32:
            return sizeof(float);
        case SampleType::FLOAT_64:
            return sizeof(double);
        default:
            return 0;
    }
}

static constexpr size_t required_buffer_size(
    const size_t num_frames,
    const size_t channels,
    const SampleType dest_sample_type)
{
    return num_frames * channels * sample_type_size(dest_sample_type);
}

void convert_sample_type(
    const size_t channels,
    uint8_t *buffer,
    const size_t source_size,
    std::vector<double> &temp_buffer,
    const SampleType source_type,
    const SampleType dest_type);
