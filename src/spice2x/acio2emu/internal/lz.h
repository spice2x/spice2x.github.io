#pragma once

#include <queue>
#include <cstdint>

namespace acio2emu::detail {
    class InflateTransformer {
    private:
        std::queue<uint8_t> output_;

        uint8_t flags_ = 0, flag_shift_ = 0;

        uint8_t window_[85] = {};
        int window_offset_  = 81;

        enum class inflateStep {
            readFlags,
            processFlags,
            copyStored,
            copyFromWindow,
        } step_ = inflateStep::readFlags;

        void window_put_(uint8_t b) {
            window_[window_offset_++] = b;
            window_offset_ %= sizeof(window_);
        }

        uint8_t window_get_(int offset) {
            return window_[offset % sizeof(window_)];
        }

    public:
        void put(uint8_t b) {
            auto consumed = false;

            while (true) {
                switch (step_) {
                case inflateStep::readFlags:
                    if (consumed) {
                        // need more data
                        return;
                    }
                    consumed = true;

                    flags_ = b;
                    flag_shift_ = 0;

                    step_ = inflateStep::processFlags;
                    break;

                case inflateStep::processFlags:
                    // have we processed every flag?
                    if (flag_shift_ > 6) {
                        step_ = inflateStep::readFlags;
                        break;
                    }

                    if (flags_ & (1 << flag_shift_)) {
                        flag_shift_++;

                        if (flags_ & (1 << flag_shift_)) {
                            // emit 0xAA when both bits are set
                            output_.push(0xAA);
                        }
                    else {
                        // copy from the window when only the lower bit is set
                        step_ = inflateStep::copyFromWindow;
                    }
                }
                else {
                    step_ = inflateStep::copyStored;
                }
                flag_shift_++;

                break;

                case inflateStep::copyFromWindow: {
                    if (consumed) {
                        // need more data
                        return;
                    }
                    consumed = true;

                    // determine the match size, default is 2-bytes
                    auto offset = b;
                    auto size = 2;

                    if (offset >= 0xAA) {
                        // 4-byte match
                        size = 4;
                        offset -= 0xAB;
                    }
                    else if (offset >= 0x55) {
                        // 3-byte match
                        size = 3;
                        offset -= 0x55;
                    }

                    for (auto i = 0; i < size; i ++) {
                        auto cur = window_get_(offset + i);

                        window_put_(cur);
                        output_.push(cur);
                    }

                    // continue processing flags
                    step_ = inflateStep::processFlags;  
                    break;
                }

                case inflateStep::copyStored:
                    if (consumed) {
                        // need more data
                        return;
                    }
                    consumed = true;

                    window_put_(b);
                    output_.push(b);

                    // continue processing flags
                    step_ = inflateStep::processFlags;  
                    break;
                }
            }
        }
        
        int get() {
            if (output_.empty()) {
                // output queue is empty
                return -1;
            }

            auto b = output_.front();
            output_.pop();

            return b;
        }
    };
}