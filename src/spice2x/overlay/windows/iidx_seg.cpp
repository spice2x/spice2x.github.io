#include <map>

#include "iidx_seg.h"
#include "games/io.h"
#include "games/iidx/iidx.h"
#include "util/logging.h"

namespace overlay::windows {

    uint32_t IIDX_SEGMENT_FONT_SIZE = 64;
    std::optional<uint32_t> IIDX_SEGMENT_FONT_COLOR = std::nullopt;
    std::string IIDX_SEGMENT_LOCATION = "bottom";

    static const size_t TICKER_SIZE = 9;
    static const ImVec4 DARK_GRAY(0.1f, 0.1f, 0.1f, 1.f);
    static const ImVec4 RED(1.f, 0.f, 0.f, 1.f);
    static const int PADDING_Y = 8;
    static const int PADDING_X = 4;

    static const std::map<char, std::pair<char, char>> CHARMAP = {\
        // period - add a space afterwards (game sends 'm' for period)
        {'m', {'.', ' '}},
        // exclamation mark (use ./ to make it look like one)
        {'!', {'.', '/'}},
        // font doesn't have tilde so using a dash instead
        {'~', {'-', '\0'}},
    };

    IIDXSegmentDisplay::IIDXSegmentDisplay(SpiceOverlay *overlay) : Window(overlay) {
        if (!DSEG_FONT) {
            log_fatal("iidx_seg", "DSEG_FONT is null");
        }

        this->title = "IIDX LED Segment Display";
        this->toggle_button = games::OverlayButtons::ToggleSubScreen;
        this->remove_window_padding = true;
        this->size_max = ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        this->flags = ImGuiWindowFlags_NoTitleBar
                      | ImGuiWindowFlags_NoScrollbar
                      | ImGuiWindowFlags_NoResize
                      | ImGuiWindowFlags_NoNavFocus
                      | ImGuiWindowFlags_NoNavInputs;

        if (IIDX_SEGMENT_FONT_COLOR.has_value()) {
            const auto rgb = IIDX_SEGMENT_FONT_COLOR.value();
            const auto rgba = IM_COL32_A_MASK | rgb;
            this->color = ImGui::ColorConvertU32ToFloat4(rgba);
        } else {
            this->color = RED;
        }
    }

    void IIDXSegmentDisplay::calculate_initial_window() {
        // ImGui::CalcTextSize doesn't seem to work here, so manually calculate
        ImGui::PushFont(DSEG_FONT);
        this->init_size = ImGui::CalcTextSize("~.~.~.~.~.~.~.~.~.");
        this->init_size.x += PADDING_X * 2;
        this->init_size.y += PADDING_Y * 2;
        ImGui::PopFont();

        // initial horizontal position
        if (IIDX_SEGMENT_LOCATION.find("left") != std::string::npos) {
            this->init_pos.x = 0;
        } else if (IIDX_SEGMENT_LOCATION.find("right") != std::string::npos) {
            this->init_pos.x = ImGui::GetIO().DisplaySize.x - this->init_size.x;
        } else {
            // center
            this->init_pos.x = ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2;
        }

        // initial vertical position
        if (IIDX_SEGMENT_LOCATION.rfind("top", 0) == 0) {
            this->init_pos.y = 0;
        } else {
            // bottom
            this->init_pos.y = ImGui::GetIO().DisplaySize.y - this->init_size.y;
        }
    }

    void IIDXSegmentDisplay::build_content() {
        char input_ticker[TICKER_SIZE];

        // get ticker content from game
        games::iidx::IIDX_LED_TICKER_LOCK.lock();
        memcpy(input_ticker, games::iidx::IIDXIO_LED_TICKER, TICKER_SIZE);
        games::iidx::IIDX_LED_TICKER_LOCK.unlock();

        // since every input character can result in up to two characters,
        // need double size for output
        const size_t TICKER_OUTPUT_SIZE = TICKER_SIZE * 2 + 1;
        char output_ticker[TICKER_OUTPUT_SIZE];
        int output_ticker_index = 0;

        // look at each input char and convert into output char(s)
        for (const auto c : input_ticker) {
            // see if there is an applicable rule for this input character
            if (0 == CHARMAP.count(c)) {
                // if not, copy the first character and keep going
                output_ticker[output_ticker_index] = c;
                output_ticker_index += 1;
                continue;
            }

            // there is a replacement rule for this input character
            auto replacements = CHARMAP.at(c);

            // replace the first character...
            output_ticker[output_ticker_index] = replacements.first;
            output_ticker_index += 1;

            // and optionally add the second character
            if (replacements.second != '\0') {
                output_ticker[output_ticker_index] = replacements.second;
                output_ticker_index += 1;
            }
        }
        if ((int)TICKER_OUTPUT_SIZE <= output_ticker_index) {
            log_fatal("iidx_seg", "{} is beyond array bounds", output_ticker_index);
        }

        // terminating null...
        output_ticker[output_ticker_index] = '\0';

        // finally, draw UI elements
        draw_ticker(output_ticker);
    }

    void IIDXSegmentDisplay::draw_ticker(char *ticker_string) {
        ImGui::PushFont(DSEG_FONT);

        const auto pos = ImVec2(PADDING_X, PADDING_Y);

        // to imitate LED "off"
        ImGui::SetCursorPos(pos);
        ImGui::PushStyleColor(ImGuiCol_Text, DARK_GRAY);
        ImGui::TextUnformatted("~.~.~.~.~.~.~.~.~.");
        ImGui::PopStyleColor();

        // ... then draw the LED "on" above it
        ImGui::SetCursorPos(pos);
        ImGui::PushStyleColor(ImGuiCol_Text, this->color);
        ImGui::TextUnformatted(ticker_string);
        ImGui::PopStyleColor();

        ImGui::PopFont();
    }
}
