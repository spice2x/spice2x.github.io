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

#ifndef NO_SCARD

#include "scard.h"
#include <windows.h>
#include <winscard.h>
#include <thread>
#include "util/utils.h"
#include "util/logging.h"
#include "hooks/sleephook.h"

#define MAX_APDU_SIZE 255
#define SCARD_POLL_INTERVAL_MS 100

// set to detect all cards, reduce polling rate to 500ms.
// based off acr122u reader, see page 26 in api document.
// https://www.acs.com.hk/en/download-manual/419/API-ACR122U-2.04.pdf
#define PICC_OPERATING_PARAMS 0xDFu
BYTE PICC_OPERATING_PARAM_CMD[5] = {0xFFu, 0x00u, 0x51u, PICC_OPERATING_PARAMS, 0x00u};

// return bytes from device
#define PICC_SUCCESS 0x90u

static const BYTE UID_CMD[5] = { 0xFFu, 0xCAu, 0x00u, 0x00u, 0x00u };

enum scard_atr_protocol {
    SCARD_ATR_PROTOCOL_ISO14443_PART3 = 0x03,
    SCARD_ATR_PROTOCOL_ISO15693_PART3 = 0x0B,
    SCARD_ATR_PROTOCOL_FELICA_212K = 0x11,
    SCARD_ATR_PROTOCOL_FELICA_424K = 0x12,
};

volatile bool should_exit = false;

winscard_config_t WINSCARD_CONFIG = {};

void scard_update(SCARDCONTEXT hContext, LPCTSTR readerName, uint8_t unit_no) {

    // Connect to the smart card.
    LONG lRet = 0;
    SCARDHANDLE hCard{};
    DWORD dwActiveProtocol{};
    for (int retry = 0; retry < 10; retry++) {
        if ((lRet = SCardConnect(hContext, readerName, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                &hCard, &dwActiveProtocol)) == SCARD_S_SUCCESS) {
            break;
        }
        Sleep(20);
    }

    if (lRet != SCARD_S_SUCCESS) {
        log_warning("scard", "error connecting to the card: 0x{:08x}", static_cast<unsigned>(lRet));
        return;
    }

    // set the reader params
    lRet = 0;
    auto pci = dwActiveProtocol == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 : SCARD_PCI_T0;
    DWORD cbRecv = MAX_APDU_SIZE;
    BYTE pbRecv[MAX_APDU_SIZE];
    if ((lRet = SCardTransmit(hCard, pci, PICC_OPERATING_PARAM_CMD, sizeof(PICC_OPERATING_PARAM_CMD),
                              nullptr, pbRecv, &cbRecv)) != SCARD_S_SUCCESS) {
        log_warning("scard", "error setting PICC params: 0x{:08x}", static_cast<unsigned>(lRet));
        return;
    }

    if(cbRecv > 2 && pbRecv[0] != PICC_SUCCESS && pbRecv[1] != PICC_OPERATING_PARAMS)
    {
        log_warning("scard", "PICC params not valid 0x{:02x} != 0x{:02x}",
                static_cast<unsigned>(pbRecv[1]), static_cast<unsigned>(PICC_OPERATING_PARAMS));
        return;
    }

    // Read ATR to determine card type.
    TCHAR szReader[200];
    DWORD cchReader = 200;
    BYTE atr[32];
    DWORD cByteAtr = 32;
    lRet = SCardStatus(hCard, szReader, &cchReader, nullptr, nullptr, atr, &cByteAtr);
    if (lRet != SCARD_S_SUCCESS) {
        log_warning("scard", "error getting card status: 0x{:08x}", static_cast<unsigned>(lRet));
        return;
    }

    // Only care about 20-byte ATRs returned by arcade-type smart cards
    if (cByteAtr != 20) {
        log_warning("scard", "ignoring card with len(atr) = {} ({})", cByteAtr, bin2hex(atr, cByteAtr));
        return;
    } else {
        // https://pcsc-tools.apdu.fr/smartcard_list.txt
        log_misc("scard", "atr string (format of card) = {}", bin2hex(atr, cByteAtr));
    }

    //log_info("scard", "atr Return: len({}), {}", cByteAtr, bin2hex(atr, cByteAtr));

    // Figure out if we should reverse the UID returned by the card based on the ATR protocol
    BYTE cardProtocol = atr[12];
    BOOL shouldReverseUid = false;
    bool is_felica = false;
    if (cardProtocol == SCARD_ATR_PROTOCOL_ISO15693_PART3) {
        log_info("scard", "card protocol: ISO15693_PART3");
        shouldReverseUid = true;
    } else if (cardProtocol == SCARD_ATR_PROTOCOL_ISO14443_PART3) {
        log_info("scard", "card protocol: ISO14443_PART3");
    } else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_212K) {
        log_info("scard", "card protocol: FELICA_212K");
        is_felica = true;
    } else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_424K) {
        log_info("scard", "card protocol: FELICA_424K");
        is_felica = true;
    } else {
        log_warning("scard", "Unknown NFC Protocol: 0x{:02x}", static_cast<unsigned>(cardProtocol));
        //return;
    }

    // Read UID
    cbRecv = MAX_APDU_SIZE;
    if ((lRet = SCardTransmit(hCard, pci, UID_CMD, sizeof(UID_CMD),nullptr, pbRecv, &cbRecv)) != SCARD_S_SUCCESS) {
        log_warning("scard", "error querying card UID: 0x{:08x}", static_cast<unsigned>(lRet));
        return;
    }

    // check response
    // 2 bytes minimum for response code
    // 3+ bytes for response code plus one byte card number
    if (cbRecv < 3) {
        if (cbRecv < 2) {
            log_warning("scard", "UID query failed - not enough bytes ({})", cbRecv);
        } else {
            log_warning("scard", "UID query failed - received {:x} {:x} ({} bytes)", pbRecv[0], pbRecv[1], cbRecv);
        }
        return;
    }

    // according to ACS's spec the response format is
    //     UID(LSB)... UID(MSB) 90 00
    // 90 00 is the success code, but previously this code was using it as if it was part of the UID
    // fix this by decrementing cbRecv, but preserve the old behavior for compatibility if no flags are set
    // (this wouldn't have mattered for actual FeliCa cards since the 9000 would have been truncated)
    if (WINSCARD_CONFIG.add_padding_to_old_cards || WINSCARD_CONFIG.add_padding_to_felica) {
        cbRecv -= 2;
    }

    if ((lRet = SCardDisconnect(hCard, SCARD_LEAVE_CARD)) != SCARD_S_SUCCESS) {
        log_info("scard", "failed SCardDisconnect: 0x{:08x}", static_cast<unsigned>(lRet));
    }

    log_misc("scard", "reader unit no: {}, raw card number from reader: {}", unit_no, bin2hex(pbRecv, cbRecv));

    if (cbRecv < 8) {
        log_misc("scard", "padding card uid to 8 bytes by adding zeroes at the end");
        memset(&pbRecv[cbRecv], 0, 8 - cbRecv);
    } else if (cbRecv > 8) {
        log_misc("scard", "taking first 8 bytes of len(uid) = {}", cbRecv);
    }

    // Copy UID to struct, reversing if necessary
    card_info_t card_info;
    if (shouldReverseUid) {
        for (DWORD i = 0; i < 8; i++) {
            card_info.uid[i] = pbRecv[7 - i];
        }
    } else {
        memcpy(card_info.uid, pbRecv, 8);
    }

    // add prefix if the user wants to override card number for compatibility
    // don't do it if the card already begins with e00401
    bool add_prefix = 
        (is_felica && WINSCARD_CONFIG.add_padding_to_felica) ||
        (!is_felica && WINSCARD_CONFIG.add_padding_to_old_cards);
    if (add_prefix &&
        !(card_info.uid[0] == 0xe0 && card_info.uid[1] == 0x04 && card_info.uid[2] == 0x01)) {
        // abcd ef12 3456 0000 => e004 01ab cdef 1234
        log_misc("scard", "adding E00401 prefix to card for compatibility (-scardfix)");

        uint8_t uid[8];
        uid[0] = 0xe0;
        uid[1] = 0x04;
        uid[2] = 0x01;
        for (DWORD i = 3; i < 8; i++) {
            uid[i] = card_info.uid[i-3];
        }
        memcpy(card_info.uid, uid, 8);
    }

    log_info("scard", "reader unit no: {}, card number to be inserted into game: {}", unit_no, bin2hex(card_info.uid, 8));

    // set card type to 1
    card_info.card_type = 1;

    // update toggle
    bool flip_order = WINSCARD_CONFIG.flip_order;
    if (WINSCARD_CONFIG.flip_order) {
        log_info("scard", "Flip order of readers since -scardflip option is set");
    }
    if (WINSCARD_CONFIG.toggle_order && (GetKeyState(VK_NUMLOCK) & 1) > 0) {
        log_info("scard", "Flip order of readers since Num Lock is on");
        flip_order = !flip_order;
    }

    // callback
    if (WINSCARD_CONFIG.cardinfo_callback) {
        WINSCARD_CONFIG.cardinfo_callback(flip_order ? ~unit_no : unit_no, &card_info);
    }
}

void scard_clear(uint8_t unitNo) {

    // reset
    card_info_t empty_cardinfo{};

    // callback
    if (WINSCARD_CONFIG.cardinfo_callback)
        WINSCARD_CONFIG.cardinfo_callback(WINSCARD_CONFIG.flip_order ? ~unitNo : unitNo, &empty_cardinfo);
}

void scard_loop(SCARDCONTEXT hContext, LPCTSTR slot0_reader_name, LPCTSTR slot1_reader_name, uint8_t reader_count) {
    LONG lRet = 0;
    SCARD_READERSTATE reader_states[2] = {
        {
            .szReader = slot0_reader_name,
            .pvUserData = nullptr,
            .dwCurrentState = SCARD_STATE_UNAWARE,
            .dwEventState = 0,
            .cbAtr = 0,
            .rgbAtr = { 0 },
        },
        {
            .szReader = slot1_reader_name,
            .pvUserData = nullptr,
            .dwCurrentState = SCARD_STATE_UNAWARE,
            .dwEventState = 0,
            .cbAtr = 0,
            .rgbAtr = { 0 },
        },
    };

    if (reader_count < 1) {
        return;
    }

    while (!should_exit) {
        lRet = SCardGetStatusChange(hContext, SCARD_POLL_INTERVAL_MS, reader_states, reader_count);
        if (lRet == SCARD_E_TIMEOUT) {
            continue;
        } else if (lRet != SCARD_S_SUCCESS) {
            log_warning("scard", "failed SCardGetStatusChange: 0x{:08x}", static_cast<unsigned>(lRet));

            // scard service disappeared, probably because the last reader was unplugged
            // we currently can't recover from this; we would need to rediscover reader devices
            // using SCardListReaders
            if (lRet == SCARD_E_SERVICE_STOPPED) {
                log_warning(
                    "scard",
                    "scard service stopped, polling will stop; "
                    "the smart card reader was likely unplugged");
                break;
            }

            // SCardGetStatusChange can return immediately on some error states;
            // wait a bit to prevent excessive polling
            Sleep(SCARD_POLL_INTERVAL_MS * 10);
            continue;
        }

        for (uint8_t unit_no = 0; unit_no < reader_count; unit_no++) {
            if (!(reader_states[unit_no].dwEventState & SCARD_STATE_CHANGED)) {
                continue;
            }

            DWORD newState = reader_states[unit_no].dwEventState ^ SCARD_STATE_CHANGED;
            bool wasCardPresent = (reader_states[unit_no].dwCurrentState & SCARD_STATE_PRESENT) > 0;
            if (newState & SCARD_STATE_UNAVAILABLE) {
                log_misc("scard", "new card state: unavailable");
                Sleep(SCARD_POLL_INTERVAL_MS);
            } else if (newState & SCARD_STATE_EMPTY) {
                log_misc("scard", "new card state: empty");
                scard_clear(unit_no);
            } else if (newState & SCARD_STATE_PRESENT && !wasCardPresent) {
                log_misc("scard", "new card state: present");
                scard_update(hContext, reader_states[unit_no].szReader, unit_no);
            }

            reader_states[unit_no].dwCurrentState = reader_states[unit_no].dwEventState;
        }
    }
}

int scard_threadmain() {
    LONG lRet = 0;
    SCARDCONTEXT hContext = 0;

    if ((lRet = SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &hContext)) != SCARD_S_SUCCESS) {
        log_warning("scard", "failed to establish SCard context: {}", bin2hex(&lRet, sizeof(LONG)));
        return lRet;
    }

    LPCTSTR reader = nullptr;
    LPTSTR reader_name_slots[2] = {nullptr, nullptr };
    int readerNameLen = 0;
    while (!(reader_name_slots[0] || reader_name_slots[1])) {

        // get list of readers
        LPTSTR reader_list = nullptr;
        auto pcchReaders = SCARD_AUTOALLOCATE;
        lRet = SCardListReaders(hContext, nullptr, (LPTSTR) &reader_list, &pcchReaders);

        int slot0_idx = -1;
        int slot1_idx = -1;
        int readerCount = 0;
        switch (lRet) {
            case SCARD_E_NO_READERS_AVAILABLE:
                log_info("scard", "no readers available, waiting 1000ms...");
                Sleep(1000);
                break;

            case SCARD_S_SUCCESS:

                // So WinAPI has this terrible "multi-string" concept wherein you have a list
                // of null-terminated strings, terminated by a double-null.
                for (reader = reader_list; *reader; reader = reader + lstrlen(reader) + 1) {
                    log_info("scard", "found reader: {}", reader);
                    if (WINSCARD_CONFIG.slot0_reader_name && !lstrcmp(WINSCARD_CONFIG.slot0_reader_name, reader))
                        slot0_idx = readerCount;
                    if (WINSCARD_CONFIG.slot1_reader_name && !lstrcmp(WINSCARD_CONFIG.slot1_reader_name, reader))
                        slot1_idx = readerCount;
                    readerCount++;
                }

                // If we have at least two readers, assign readers to slots as necessary.
                if (readerCount >= 2) {
                    if (slot1_idx != 0)
                        slot0_idx = 0;
                    if (slot0_idx != 1)
                        slot1_idx = 1;
                }

                // if the reader count is 1 and no reader was set, set first reader
                if (readerCount == 1 && slot0_idx < 0 && slot1_idx < 0)
                    slot0_idx = 0;

                // If we somehow only found slot 1, promote slot 1 to slot 0.
                if (slot0_idx < 0 && slot1_idx >= 0) {
                    slot0_idx = slot1_idx;
                    slot1_idx = -1;
                }

                // Extract the relevant names from the multi-string.
                int i;
                for (i = 0, reader = reader_list; *reader; reader = reader + lstrlen(reader) + 1, i++) {
                    if (slot0_idx == i) {
                        readerNameLen = lstrlen(reader);
                        reader_name_slots[0] = (LPTSTR) HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS,
                                                                sizeof(TCHAR) * (readerNameLen + 1));
                        memcpy(reader_name_slots[0], &reader[0], (size_t) (readerNameLen + 1));
                    }
                    if (slot1_idx == i) {
                        readerNameLen = lstrlen(reader);
                        reader_name_slots[1] = (LPTSTR) HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS,
                                                                sizeof(TCHAR) * (readerNameLen + 1));
                        memcpy(reader_name_slots[1], &reader[0], (size_t) (readerNameLen + 1));
                    }
                }
                break;

            default:
                log_warning("scard", "failed SCardListReaders: 0x{:08x}", lRet);
                Sleep(5000);
                break;
        }

        if (reader_list) {
            SCardFreeMemory(hContext, reader_list);
        }
    }

    if (reader_name_slots[0]) {
        log_info("scard", "using reader slot 0: {}", reader_name_slots[0]);
    }
    if (reader_name_slots[1]) {
        log_info("scard", "using reader slot 1: {}", reader_name_slots[1]);
    }

    scard_loop(hContext, reader_name_slots[0], reader_name_slots[1], (uint8_t) (reader_name_slots[1] ? 2 : 1));

    if (reader_name_slots[0]) {
        HeapFree(GetProcessHeap(), 0, reader_name_slots[0]);
    }
    if (reader_name_slots[1]) {
        HeapFree(GetProcessHeap(), 0, reader_name_slots[1]);
    }
    log_misc("scard", "exiting main thread");
    return 0;
}

void scard_threadstart() {
    std::thread t(scard_threadmain);
    t.detach();
}

void scard_fini() {
    should_exit = true;
}

#endif
