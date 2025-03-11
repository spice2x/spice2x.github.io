/**
 * MIT-License
 * Copyright (c) 2018 by nolm <nolan@nolm.name>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Modified version.
 */

#pragma once

#include <cstdint>

// cardinfo_t is a description of a card that was presented to a reader
typedef struct card_info {
    int card_type = 0;
    uint8_t uid[8];
} card_info_t;

/*
 * cardinfo_callback_t is a pointer to a function that will be called on card present.
 *
 * The provided cardinfo_t pointer will become invalid after the callback returns, so
 * any data contained within should be copied into more permanent storage if necessary.
 */
typedef void (*cardinfo_callback_t)(uint8_t slot_no, card_info_t *cardinfo);

// winscard_config_t describes the parameters used to configure the smart card listener.
typedef struct {

    // A pointer to a null-terminated string that specifies the name of the reader used for
    // the first slot, according to SCardListReaders.
    //
    // If this is NULL or a zero-length string, the first reader found will be used.
    const char *slot0_reader_name;

    // A pointer to a null-terminated string that specifies the name of the reader used for
    // the second slot, according to SCardListReaders. If only this slot is set, or the reader
    // specified for the first slot is not found, this reader will become the second slot.
    //
    // If this is NULL or a zero-length string, the second reader found will be used, if any.
    const char *slot1_reader_name;

    // A pointer to a function that will be called when a card state change event has occurred.
    //
    // If a card has been presented, cardinfo_t.card_type will be set accordingly. If a card has
    // been removed, then card_type will be 0, and the value of uid is undefined.
    cardinfo_callback_t cardinfo_callback;

    // Flips the order of the card insertions for P1/P2
    bool flip_order;

    // For toggling the order using NUMLOCK
    bool toggle_order;

    bool add_padding_to_old_cards;
    bool add_padding_to_felica;

} winscard_config_t;
extern winscard_config_t WINSCARD_CONFIG;

/* scard_threadmain is the entry point for smart card listener thread. */
int scard_threadmain();

/* start threadmain in a separate thread */
void scard_threadstart();

/* scard_fini signals to terminate and clean up smart card listener routine. */
void scard_fini();
