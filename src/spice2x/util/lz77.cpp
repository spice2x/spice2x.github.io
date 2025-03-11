#include "lz77.h"
#include <cstdlib>

namespace util::lz77 {

    /*
     * Configuration Values
     */
    static const size_t LZ_WINDOW_SIZE = 0x1000;
    static const size_t LZ_WINDOW_MASK = LZ_WINDOW_SIZE - 1;
    static const size_t LZ_THRESHOLD = 3;
    static const size_t LZ_THRESHOLD_INPLACE = 0xA;
    static const size_t LZ_LOOK_RANGE = 0x200;
    static const size_t LZ_MAX_LEN = 0xF + LZ_THRESHOLD;
    static const size_t LZ_MAX_BUFFER = 0x10 + 1;

    /*
     * Dummy Compression
     * Results in even bigger output size but is fast af.
     */
    uint8_t *compress_stub(uint8_t *input, size_t input_length, size_t *compressed_length) {
        uint8_t *output = (uint8_t *) malloc(input_length + input_length / 8 + 9);
        uint8_t *cur_byte = &output[0];

        // copy data blocks
        for (size_t n = 0; n < input_length / 8; n++) {

            // fake flag
            *cur_byte++ = 0xFF;

            // uncompressed data
            for (size_t i = 0; i < 8; i++) {
                *cur_byte++ = input[n * 8 + i];
            }
        }

        // remaining bytes
        int extra_bytes = input_length % 8;
        if (extra_bytes == 0) {
            *cur_byte++ = 0x00;
        } else {
            *cur_byte++ = 0xFF >> (8 - extra_bytes);
            for (size_t i = input_length - extra_bytes; i < input_length; i++) {
                *cur_byte++ = input[i];
            }
            for (size_t i = 0; i < 4; i++) {
                *cur_byte++ = 0x00;
            }
        }

        // calculate size
        *compressed_length = (size_t) (cur_byte - &output[0]);
        return output;
    }

    /*
     * Compression Helpers
     */

    static size_t match_current(uint8_t *window, size_t pos, size_t length_max,
            uint8_t *data, uint8_t data_length, size_t data_pos) {

        // compare data
        size_t length = 0;
        while ((data_pos + length < data_length) &&
               (length < length_max) &&
               (window[(pos + length) & LZ_WINDOW_MASK] == data[data_pos + length] &&
                length < LZ_MAX_LEN)) {
            length++;
        }

        // return detected length
        return length;
    }

    static bool match_window(uint8_t *window, size_t pos, uint8_t *data, size_t data_length, size_t data_pos,
            size_t *match_pos, size_t *match_length) {

        // search window for best match
        size_t pos_max = 0, length_max = 0;
        for (size_t i = LZ_THRESHOLD; i < LZ_LOOK_RANGE; i++) {

            // check for match
            size_t length = match_current(window, (pos - i) & LZ_WINDOW_MASK, i, data, data_length, data_pos);

            // check if threshold is reached
            if (length >= LZ_THRESHOLD_INPLACE) {
                *match_pos = i;
                *match_length = length;
                return true;
            }

            // update max values
            if (length >= LZ_THRESHOLD) {
                pos_max = i;
                length_max = length;
            }
        }

        // check if threshold is reached
        if (length_max >= LZ_THRESHOLD) {
            *match_pos = pos_max;
            *match_length = length_max;
            return true;
        } else {
            return false;
        }
    }

    /*
     * This one actually compresses the data.
     */
    std::vector<uint8_t> compress(uint8_t *input, size_t input_length) {

        // output buffer
        std::vector<uint8_t> output;
        output.reserve(input_length);

        // window buffer
        uint8_t *window = new uint8_t[LZ_WINDOW_SIZE];
        size_t window_pos = 0;

        // working buffer
        uint8_t *buffer = new uint8_t[LZ_MAX_BUFFER];
        size_t buffer_pos = 0;

        // state
        uint8_t flag;
        size_t pad = 3;

        // iterate input bytes
        size_t input_pos = 0;
        while (input_pos < input_length) {

            // reset state
            flag = 0;
            buffer_pos = 0;

            // iterate flag bytes
            for (size_t bit_pos = 0; bit_pos < 8; bit_pos++) {

                // don't match if data is bigger than input
                if (input_pos >= input_length) {
                    pad = 0;
                    flag = flag >> (8 - bit_pos);
                    buffer[buffer_pos++] = 0;
                    buffer[buffer_pos++] = 0;
                }

                // match window
                uint8_t bit_value;
                size_t match_pos, match_length;
                if (match_window(window, window_pos, input, input_length, input_pos, &match_pos, &match_length)) {

                    // apply match
                    buffer[buffer_pos++] = match_pos >> 4;
                    buffer[buffer_pos++] = ((match_pos & 0xF) << 4) | ((match_length - LZ_THRESHOLD) & 0xF);
                    bit_value = 0;
                    for (size_t n = 0; n < match_length; n++) {
                        window[window_pos++ & LZ_WINDOW_MASK] = input[input_pos++];
                    }

                } else {

                    // no match found
                    buffer[buffer_pos++] = input[input_pos];
                    window[window_pos++] = input[input_pos++];
                    bit_value = 1;
                }

                // add bit to flags
                flag = (flag >> 1) & 0x7F;
                flag |= bit_value == 0 ? 0 : 0x80;

                // update window
                window_pos &= LZ_WINDOW_MASK;
            }

            // write to output
            output.push_back(flag);
            for (size_t i = 0; i < buffer_pos; i++) {
                output.push_back(buffer[i]);
            }
        }

        // padding
        for (size_t i = 0; i < pad; i++) {
            output.push_back(0x00);
        }

        // clean up and return result
        delete[] window;
        delete[] buffer;
        return output;
    }

    std::vector<uint8_t> decompress(uint8_t *input, size_t input_length) {

        // output buffer
        std::vector<uint8_t> output;
        output.reserve(input_length);

        // create window
        uint8_t *window = new uint8_t[LZ_WINDOW_SIZE];
        size_t window_pos = 0;

        // iterate input data
        size_t input_pos = 0;
        while (input_pos < input_length) {

            // read flag
            uint8_t flag = input[input_pos++];

            // iterate flag bits
            for (size_t bit_pos = 0; bit_pos < 8; bit_pos++) {

                // check flag bit
                if ((flag >> bit_pos) & 1) {

                    // copy data from input
                    output.push_back(input[input_pos]);
                    window[window_pos++] = input[input_pos++];
                    window_pos &= LZ_WINDOW_MASK;

                } else if (input_pos + 1 < input_length) {

                    // check word
                    size_t word = (input[input_pos] << 8) | input[input_pos + 1];
                    if (word == 0) {

                        // detected end
                        delete[] window;
                        return output;

                    } else {

                        // skip word
                        input_pos += 2;

                        // get window
                        size_t position = (window_pos - (word >> 4)) & LZ_WINDOW_MASK;
                        size_t length = (word & 0x0F) + LZ_THRESHOLD;

                        // copy data from window
                        for (size_t i = 0; i < length; i++) {
                            uint8_t data = window[position++ & LZ_WINDOW_MASK];
                            output.push_back(data);
                            window[window_pos++] = data;
                            window_pos &= LZ_WINDOW_MASK;
                        }
                    }
                }
            }
        }

        // clean up and return result
        delete[] window;
        return output;
    }
}
