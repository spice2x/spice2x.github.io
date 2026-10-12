#include "printer.h"

#if SPICE64

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>

#include "games/shared/printer.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"

namespace games::cardconnect {

    // Settings and native protocol values.
    bool PRINTER_DISABLED = false;
    bool PRINTER_SIMULATE_DELAY = false;

    static constexpr uint16_t READY = 0;
    static constexpr uint16_t INVALID_PARAMETER = 1006;
    static constexpr uint16_t BUSY = 1500;
    static constexpr uint16_t PRINTING = 2100;
    static constexpr uint16_t OUTPUT_ERROR = 2504;

    enum PrinterInfoTag : uint16_t {
        INFO_PAPER = 0,
        INFO_FIRMWARE = 3,
        INFO_PRINT_COUNT = 4,
        INFO_HOLO_REMAINING = 6,
        INFO_MEMORY = 16,
        INFO_FRONT_MODE = 19,
        INFO_BACK_MODE = 20,
        INFO_IR_CORRECTION = 22,
    };

    enum PrinterMode : uint8_t {
        MODE_YMC_MAT = 2,
        MODE_YMC_LAM = 3,
        MODE_YMC_MAT_HOLO = 5,
        MODE_YMC_LAM_HOLO = 6,
        MODE_IR_R_YMAC_MAT = 8,
        MODE_IR_R_YMAC_LAM = 9,
        MODE_R_YMAC_MAT = 10,
        MODE_R_YMAC_LAM = 11,
        MODE_YMC_GROSS = 18,
        MODE_YMC_GROSS_HOLO = 21,
        MODE_IR_R_YMAC_GROSS = 24,
        MODE_R_YMAC_GROSS = 25,
    };

    static constexpr uint16_t FORMAT_RGB = 3;
    static constexpr uint16_t FORMAT_BGR = 4;
    static constexpr uint16_t COLOR_COMPONENTS = 3;
    static constexpr uint16_t COLOR_DEPTH = 8;
    static constexpr uint16_t CARD_WIDTH = 662;
    static constexpr uint16_t CARD_HEIGHT = 1024;
    static constexpr uint32_t RIBBON_REMAINING = 4096;
    static constexpr auto PRINT_TIME = std::chrono::seconds(5);

    static constexpr std::optional<std::chrono::seconds> get_mode_delay(uint8_t mode) {
        // Printer::GetPrintTime adds 10s for holo and 8s per finished side.
        if (mode == MODE_YMC_GROSS || mode == MODE_IR_R_YMAC_GROSS || mode == MODE_R_YMAC_GROSS) {
            return std::chrono::seconds(0);
        }
        if (mode == MODE_YMC_MAT || mode == MODE_YMC_LAM ||
            mode == MODE_IR_R_YMAC_MAT || mode == MODE_IR_R_YMAC_LAM ||
            mode == MODE_R_YMAC_MAT || mode == MODE_R_YMAC_LAM) {
            return std::chrono::seconds(8);
        }
        if (mode == MODE_YMC_GROSS_HOLO) {
            return std::chrono::seconds(10);
        }
        if (mode == MODE_YMC_MAT_HOLO || mode == MODE_YMC_LAM_HOLO) {
            return std::chrono::seconds(18);
        }
        return std::nullopt;
    }

    // Native reply layouts.
#pragma pack(push, 1)
    struct FirmwareRecord {
        std::array<uint8_t, 34> reserved;
        uint8_t major;
        uint8_t minor;
        std::array<uint8_t, 2> reserved_tail;
    };

    struct FirmwareReply {
        uint8_t count;
        std::array<FirmwareRecord, 4> records;
    };

    struct PrintCountReply {
        uint32_t total_count;
        std::array<uint8_t, 16> reserved;
        uint32_t ribbon_remaining;
        std::array<uint8_t, 8> reserved_tail;
    };

    struct HoloRemainingReply {
        uint32_t remaining;
        std::array<uint8_t, 28> reserved;
    };

    struct MemoryReply {
        uint16_t reserved;
        uint16_t remaining;
        uint16_t completed;
        uint16_t copies;
        std::array<uint8_t, 10> reserved_tail;
    };

    struct PrintIDReply {
        uint16_t remaining;
        std::array<uint8_t, 4> reserved;
        uint16_t status;
    };
#pragma pack(pop)

    static_assert(sizeof(FirmwareRecord) == 38);
    static_assert(offsetof(FirmwareRecord, major) == 34);
    static_assert(offsetof(FirmwareRecord, minor) == 35);
    static_assert(sizeof(FirmwareReply) == 153);
    static_assert(offsetof(FirmwareReply, records) == 1);

    static_assert(sizeof(PrintCountReply) == 32);
    static_assert(offsetof(PrintCountReply, ribbon_remaining) == 20);
    static_assert(sizeof(HoloRemainingReply) == 32);

    static_assert(sizeof(MemoryReply) == 18);
    static_assert(offsetof(MemoryReply, remaining) == 2);
    static_assert(offsetof(MemoryReply, completed) == 4);
    static_assert(offsetof(MemoryReply, copies) == 6);

    static_assert(sizeof(PrintIDReply) == 8);
    static_assert(offsetof(PrintIDReply, status) == 6);

    // One printer, shared by the game's threads.
    struct PrintJob {
        uint16_t id = 0;
        uint16_t copies = 1;
        uint16_t remaining = 0;

        uint16_t width = 0;
        uint16_t height = 0;
        uint16_t format = FORMAT_RGB;
        std::array<std::vector<uint8_t>, 2> sides;

        uint16_t error = READY;
        bool receiving = false;
        // Submitted jobs stay busy while waiting for this baseline.
        bool awaiting_counter_baseline = false;
        std::array<std::chrono::seconds, 2> mode_delays {};
        std::chrono::seconds print_time = PRINT_TIME;
        std::chrono::steady_clock::time_point next_copy;
    };

    static std::mutex printer_mutex;
    static PrintJob job;
    static uint16_t next_page_id = 0;
    static uint32_t total_count = 0;
    static uint32_t cutter_offset = 0;

    // Write the result; non-ready codes return FALSE.
    static BOOL set_result(uint16_t *result, uint16_t code = READY) {
        if (!result) {
            log_warning("cardconnect::printer", "missing result buffer");
            return FALSE;
        }

        *result = code;
        return code == READY;
    }

    static BOOL invalid_parameter(uint16_t *result, const char *operation) {
        log_warning("cardconnect::printer", "{}: invalid parameter or job state", operation);
        return set_result(result, INVALID_PARAMETER);
    }

    // Caller holds printer_mutex. The baseline gates completion.
    static void update_printing() {
        if (job.awaiting_counter_baseline) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        while (job.remaining && now >= job.next_copy) {
            ++total_count;
            --job.remaining;
            job.next_copy += job.print_time;
            log_info("cardconnect::printer", "job {}: total count={}, remaining={}",
                    job.id, total_count, job.remaining);
        }
    }

    // Caller holds printer_mutex.
    static uint16_t poll_status() {
        update_printing();
        if (job.error) {
            return job.error;
        }
        return job.remaining ? PRINTING : READY;
    }

    // Driver lifetime.
    static void __stdcall chcusb_MakeThread(uint16_t count) {
        log_info("cardconnect::printer", "initializing printer with {} thread slots", count);
    }

    static BOOL __stdcall chcusb_open(uint16_t *result) {
        return set_result(result);
    }

    static void __stdcall chcusb_close() {
        // Closing a worker must not cancel a submitted job.
        std::lock_guard lock(printer_mutex);
        if (job.receiving) {
            log_warning("cardconnect::printer", "discarding incomplete print job {}", job.id);
            job = {};
        }
    }

    static BOOL __stdcall chcusb_ReleaseThread(uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        job = {};
        return set_result(result);
    }

    // Status and printer information.
    static BOOL __stdcall chcusb_status(uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        return set_result(result, poll_status());
    }

    static BOOL __stdcall chcusb_getPrinterInfo(uint16_t tag, void *buffer, uint16_t *length) {
        std::lock_guard lock(printer_mutex);
        update_printing();

        // Build the requested reply.
        std::array<uint8_t, sizeof(FirmwareReply)> data {};
        uint16_t size;
        switch (tag) {
            case INFO_FIRMWARE: {
                FirmwareReply reply {};
                reply.count = static_cast<uint8_t>(reply.records.size());
                for (auto &record : reply.records) {
                    record.major = 1;
                }
                size = sizeof(reply);
                std::memcpy(data.data(), &reply, size);
                break;
            }
            case INFO_PRINT_COUNT: {
                PrintCountReply reply {};
                reply.total_count = total_count;
                reply.ribbon_remaining = RIBBON_REMAINING;
                size = sizeof(reply);
                std::memcpy(data.data(), &reply, size);
                break;
            }
            case INFO_HOLO_REMAINING: {
                HoloRemainingReply reply {};
                reply.remaining = RIBBON_REMAINING;
                size = sizeof(reply);
                std::memcpy(data.data(), &reply, size);
                break;
            }
            case INFO_MEMORY: {
                MemoryReply reply {};
                reply.remaining = job.remaining;
                reply.completed = static_cast<uint16_t>(job.copies - job.remaining);
                reply.copies = job.copies;
                size = sizeof(reply);
                std::memcpy(data.data(), &reply, size);
                break;
            }
            default:
                log_warning("cardconnect::printer", "unsupported printer info tag {}", tag);
                return FALSE;
        }

        // Check capacity before writing.
        if (!buffer || !length || *length < size) {
            log_warning("cardconnect::printer", "invalid buffer for printer info tag {}", tag);
            if (length) {
                *length = size;
            }
            return FALSE;
        }
        std::memcpy(buffer, data.data(), size);
        *length = size;

        // CoMain needs its post-cleanup baseline before any copy finishes.
        if (tag == INFO_PRINT_COUNT && job.awaiting_counter_baseline) {
            job.awaiting_counter_baseline = false;
            job.next_copy = std::chrono::steady_clock::now() + job.print_time;
            log_info("cardconnect::printer",
                    "job {}: counter baseline={}, starting print timer ({}s per copy)",
                    job.id, total_count, job.print_time.count());
        }

        return TRUE;
    }

    // Paper and coating settings do not alter saved RGB data.
    static BOOL __stdcall chcusb_setPrinterInfo(
            uint16_t tag, const void *buffer, uint16_t *length, uint16_t *result) {
        uint16_t expected = 0;
        switch (tag) {
            case INFO_PAPER:
                expected = 10;
                break;
            case INFO_FRONT_MODE:
            case INFO_BACK_MODE:
            case INFO_IR_CORRECTION:
                // The managed caller sends one byte, not two.
                expected = 1;
                break;
        }

        if (!expected || !buffer || !length || *length != expected) {
            log_warning("cardconnect::printer",
                    "setPrinterInfo: tag={}, length={}, expected={}, buffer={}, length pointer={}",
                    tag, length ? *length : 0, expected, buffer != nullptr, length != nullptr);
            return invalid_parameter(result, "setPrinterInfo");
        }

        if (PRINTER_SIMULATE_DELAY && (tag == INFO_FRONT_MODE || tag == INFO_BACK_MODE)) {
            const auto mode = *static_cast<const uint8_t *>(buffer);
            const auto delay = get_mode_delay(mode);
            if (!delay) {
                log_warning("cardconnect::printer", "cannot estimate delay for print mode {}", mode);
                return invalid_parameter(result, "setPrinterInfo print mode");
            }
            if (!result) {
                return set_result(result);
            }

            std::lock_guard lock(printer_mutex);
            if (tag == INFO_FRONT_MODE) {
                job.mode_delays[0] = *delay;
            } else {
                job.mode_delays[1] = *delay;
            }
        }

        return set_result(result);
    }

    // Configure the next job.
    static BOOL __stdcall chcusb_imageformat(
            uint16_t format, uint16_t components, uint16_t depth,
            uint16_t width, uint16_t height, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        update_printing();

        if ((format != FORMAT_RGB && format != FORMAT_BGR) ||
            components != COLOR_COMPONENTS || depth != COLOR_DEPTH ||
            width != CARD_WIDTH || height != CARD_HEIGHT || job.receiving || job.remaining) {
            return invalid_parameter(result, "imageformat");
        }

        job.width = width;
        job.height = height;
        job.format = format;
        return set_result(result);
    }

    static BOOL __stdcall chcusb_copies(uint16_t copies, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        update_printing();

        if (!copies || job.receiving || job.remaining) {
            return invalid_parameter(result, "copies");
        }

        job.copies = copies;
        return set_result(result);
    }

    // Receive front and back image data.
    static BOOL __stdcall chcusb_startpage(uint16_t mode, uint16_t *page_id, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        update_printing();

        if (job.receiving || job.remaining) {
            log_warning("cardconnect::printer", "startpage while printer is busy");
            return set_result(result, BUSY);
        }
        if (mode != 0 || !page_id || !result || !job.width || !job.height) {
            return invalid_parameter(result, "startpage");
        }

        // Keep zero reserved for "no job".
        if (++next_page_id == 0) {
            ++next_page_id;
        }

        job.id = next_page_id;
        job.print_time = PRINT_TIME;
        if (PRINTER_SIMULATE_DELAY) {
            job.print_time = std::chrono::seconds(73) + job.mode_delays[0] + job.mode_delays[1];
        }
        job.error = READY;
        job.receiving = true;
        job.sides = {};
        *page_id = job.id;
        log_info("cardconnect::printer", "receiving job {} ({} copies)", job.id, job.copies);
        return set_result(result);
    }

    static BOOL __stdcall chcusb_write(
            const uint8_t *data, uint32_t *size, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        const size_t expected = static_cast<size_t>(job.width) * job.height * COLOR_COMPONENTS;

        if (!job.receiving || !data || !size || !result || !*size) {
            return invalid_parameter(result, "write");
        }

        // Fill the front before accepting the back.
        auto &side = job.sides[job.sides[0].size() == expected ? 1 : 0];
        if (*size > expected - side.size()) {
            return invalid_parameter(result, "write size");
        }
        side.insert(side.end(), data, data + *size);
        return set_result(result);
    }

    // Accept coating masks without saving them.
    static BOOL __stdcall discard_mask(
            const uint8_t *data, uint32_t *size, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        if (!job.receiving || !data || !size ||
            *size != static_cast<uint32_t>(job.width) * job.height) {
            return invalid_parameter(result, "mask write");
        }
        return set_result(result);
    }

    static BOOL __stdcall chcusb_endpage(uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        const size_t expected = static_cast<size_t>(job.width) * job.height * COLOR_COMPONENTS;

        // Both sides must be complete.
        if (!result || !job.receiving ||
            job.sides[0].size() != expected || job.sides[1].size() != expected) {
            return invalid_parameter(result, "endpage");
        }

        // Image files use RGB channel order.
        if (job.format == FORMAT_BGR) {
            for (auto &side : job.sides) {
                for (size_t i = 0; i < side.size(); i += COLOR_COMPONENTS) {
                    std::swap(side[i], side[i + 2]);
                }
            }
        }

        // Save every copy before reporting success.
        job.receiving = false;
        for (uint16_t i = 0; i < job.copies; ++i) {
            if (!games::shared::printer_write_images({
                    {job.sides[0].data(), job.width, job.height, COLOR_COMPONENTS, "_front"},
                    {job.sides[1].data(), job.width, job.height, COLOR_COMPONENTS, "_back"}})) {
                job.error = OUTPUT_ERROR;
                job.sides = {};
                log_warning("cardconnect::printer", "failed to save job {}", job.id);
                return set_result(result, job.error);
            }
        }

        // Wait for the game's counter baseline.
        job.sides = {};
        job.remaining = job.copies;
        job.awaiting_counter_baseline = true;
        log_info("cardconnect::printer", "submitted job {}, waiting for counter baseline", job.id);
        return set_result(result);
    }

    // Job progress and cancellation.
    static BOOL __stdcall chcusb_getPrintIDStatus(
            uint16_t page_id, void *buffer, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        const auto status = poll_status();

        if (!buffer || page_id != job.id) {
            return invalid_parameter(result, "getPrintIDStatus");
        }

        PrintIDReply reply {};
        reply.remaining = job.remaining;
        reply.status = status;
        std::memcpy(buffer, &reply, sizeof(reply));
        return set_result(result);
    }

    static BOOL __stdcall chcusb_cancelCopies(uint16_t page_id, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        if (page_id != job.id) {
            return invalid_parameter(result, "cancelCopies");
        }

        update_printing();
        job.remaining = 0;
        job.receiving = false;
        job.awaiting_counter_baseline = false;
        job.sides = {};
        return set_result(result);
    }

    static BOOL __stdcall chcusb_resetPrinter(uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        update_printing();
        job = {};
        return set_result(result);
    }

    // Physical-printer controls.
    static BOOL __stdcall chcusb_setIcctable(
            [[maybe_unused]] const char *input_profile,
            [[maybe_unused]] const char *output_profile,
            [[maybe_unused]] uint16_t rendering_intents,
            [[maybe_unused]] const void *input_tone_r,
            [[maybe_unused]] const void *input_tone_g,
            [[maybe_unused]] const void *input_tone_b,
            [[maybe_unused]] const void *output_tone_r,
            [[maybe_unused]] const void *output_tone_g,
            [[maybe_unused]] const void *output_tone_b,
            uint16_t *result) {
        // Keep the original RGB colors.
        return set_result(result);
    }

    static void __stdcall chcusb_setCutterOffset(uint32_t offset, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        cutter_offset = offset;
        set_result(result);
    }

    static BOOL __stdcall chcusb_getCutterOffset(uint32_t *offset, uint16_t *result) {
        std::lock_guard lock(printer_mutex);
        if (!offset) {
            return invalid_parameter(result, "getCutterOffset");
        }
        *offset = cutter_offset;
        return set_result(result);
    }

    static BOOL __stdcall chcusb_getErrorLog(uint16_t index, void *buffer, uint16_t *result) {
        if (!buffer || index != 0) {
            return invalid_parameter(result, "getErrorLog");
        }
        std::memset(buffer, 0, 16);
        return set_result(result);
    }

    template<typename T>
    static void hook(const char *name, T function) {
        T original = nullptr;
        detour::trampoline("C320Dusb.dll", name, function, &original);
    }

    void printer_attach() {
        if (PRINTER_DISABLED) {
            return;
        }

        log_info("cardconnect::printer", "enabling C320D printer emulation");
        games::shared::printer_init_output({"_front", "_back"});
        libutils::load_library("game\\tcs_data\\Plugins\\C320Dusb.dll");

        // Connections and status.
        hook("chcusb_MakeThread", chcusb_MakeThread);
        hook("chcusb_open", chcusb_open);
        hook("chcusb_close", chcusb_close);
        hook("chcusb_ReleaseThread", chcusb_ReleaseThread);
        hook("chcusb_status", chcusb_status);
        hook("chcusb_getPrinterInfo", chcusb_getPrinterInfo);
        hook("chcusb_setPrinterInfo", chcusb_setPrinterInfo);

        // Job setup and image transfer.
        hook("chcusb_imageformat", chcusb_imageformat);
        hook("chcusb_copies", chcusb_copies);
        hook("chcusb_startpage", chcusb_startpage);
        hook("chcusb_write", chcusb_write);
        hook("chcusb_writeLaminate", discard_mask);
        hook("chcusb_writeHolo", discard_mask);
        hook("chcusb_writeIred", discard_mask);
        hook("chcusb_endpage", chcusb_endpage);

        // Completion and device controls.
        hook("chcusb_getPrintIDStatus", chcusb_getPrintIDStatus);
        hook("chcusb_cancelCopies", chcusb_cancelCopies);
        hook("chcusb_resetPrinter", chcusb_resetPrinter);
        hook("chcusb_setIcctable", chcusb_setIcctable);
        hook("chcusb_setCutterOffset", chcusb_setCutterOffset);
        hook("chcusb_getCutterOffset", chcusb_getCutterOffset);
        hook("chcusb_getErrorLog", chcusb_getErrorLog);
    }
}

#else

namespace games::cardconnect {
    bool PRINTER_DISABLED = false;
    bool PRINTER_SIMULATE_DELAY = false;
}

#endif
