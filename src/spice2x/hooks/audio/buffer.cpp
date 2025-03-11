#include "buffer.h"

void convert_sample_type(
    const size_t channels,
    uint8_t *buffer,
    const size_t source_size,
    std::vector<double> &temp_buffer,
    const SampleType source_type,
    const SampleType dest_type)
{
    // fast case: same type
    if (source_type == dest_type) {
        return;
    }

    const size_t source_sample_size = sample_type_size(source_type);

    // number of samples *per channel*
    const size_t num_samples = source_size / source_sample_size / channels;

    // calculate the required size for the temporary buffer
    // samples are converted to doubles and then converted to the target sample type
    const size_t temp_size = num_samples * channels;

    // resize temporary buffer if needed
    if (temp_buffer.size() < temp_size) {
        temp_buffer.resize(temp_size);
    }

#define SAMPLE_LOOP(VAR) for (size_t VAR = 0; VAR < num_samples * channels; VAR++)

// converts to double in temporary buffer
#define CONVERT_LOOP(TY) \
    do { \
        const auto source = reinterpret_cast<TY *>(buffer); \
        SAMPLE_LOOP(i) { \
            temp_buffer[i] = convert_number_to_double<TY>(source[i]); \
        } \
    } while (0)

// converts double back to desired format
#define STORE_LOOP(TY) \
    do { \
        const auto dest = reinterpret_cast<TY *>(buffer); \
        SAMPLE_LOOP(i) { \
            dest[i] = convert_double_to_number<TY>(temp_buffer[i]); \
        } \
    } while (0)

// converts double to float
#define STORE_LOOP_FLOAT() \
    do { \
        const auto dest = reinterpret_cast<float *>(buffer); \
        SAMPLE_LOOP(i) { \
            dest[i] = static_cast<float>(temp_buffer[i]); \
        } \
    } while (0)

    if (source_type == SampleType::SINT_16) {
        CONVERT_LOOP(int16_t);
    } else if (source_type == SampleType::SINT_24) {
        const auto source = reinterpret_cast<int24_t *>(buffer);

        SAMPLE_LOOP(i) {
            temp_buffer[i] = convert_number_to_double<int32_t, int24_t>(source[i].as_int());
        }
    } else if (source_type == SampleType::SINT_32) {
        CONVERT_LOOP(int32_t);
    } else if (source_type == SampleType::FLOAT_32) {
        const auto source = reinterpret_cast<float *>(buffer);

        SAMPLE_LOOP(i) {
            temp_buffer[i] = source[i];
        }
    } else if (source_type == SampleType::FLOAT_64) {
        memcpy(temp_buffer.data(), buffer, temp_size);
    } else {
        return;
    }

    if (dest_type == SampleType::SINT_16) {
        STORE_LOOP(int16_t);
    } else if (dest_type == SampleType::SINT_24) {
        STORE_LOOP(int24_t);
    } else if (dest_type == SampleType::SINT_32) {
        STORE_LOOP(int32_t);
    } else if (dest_type == SampleType::FLOAT_32) {
        STORE_LOOP_FLOAT();
    } else if (dest_type == SampleType::FLOAT_64) {
        memcpy(buffer, temp_buffer.data(), temp_size);
    } else {
        return;
    }

#undef STORE_LOOP_FLOAT
#undef STORE_LOOP
#undef CONVERT_LOOP
#undef SAMPLE_LOOP
}
