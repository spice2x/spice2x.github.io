#include "rawinput.h"

#include <sstream>

#include "util/logging.h"
#include "util/time.h"
#include "util/utils.h"

namespace rawinput {

    static MidiNoteAlgorithm MIDI_NOTE_ALGORITHM = MidiNoteAlgorithm::V2;
}

rawinput::MidiNoteAlgorithm rawinput::get_midi_algorithm() {
    return rawinput::MIDI_NOTE_ALGORITHM;
}

void rawinput::set_midi_algorithm(rawinput::MidiNoteAlgorithm new_algo) {
    rawinput::MIDI_NOTE_ALGORITHM = new_algo;
    std::string s = "Unknown";
    switch (new_algo) {
        case rawinput::MidiNoteAlgorithm::LEGACY:
            s = "legacy";
            break;
        case rawinput::MidiNoteAlgorithm::V2:
            s = "v2";
            break;
        case rawinput::MidiNoteAlgorithm::V2_DRUM:
            s = "v2_drum";
            break;
        default:
            log_info("rawinput", "assert failed: invalid midi algorithm");
            break;
    }
    log_info("rawinput", "using MIDI algorithm: {}", s);
}

void rawinput::RawInputManager::midi_scan_start() {

    // single-flight: only one scan runs at a time. if one is already running, set
    // the pending flag so it rescans once more when it finishes - MIDI hotplug
    // events fire while the slow enumeration is still going and must not be lost.
    // the scheduler mutex makes this check-and-set atomic with the worker's
    // exit-or-rescan decision below, so a request set while a scan is running is
    // never dropped
    {
        std::lock_guard<std::mutex> lock(this->midi_scan_m);
        if (this->midi_scan_active) {
            this->midi_scan_pending = true;
            log_misc("rawinput", "MIDI scan already running, queued rescan");
            return;
        }
        this->midi_scan_active = true;
        this->midi_scan_pending = false;
    }

    // clean up the previous (already finished) scan thread handle
    this->midi_scan_join();

    // run the (potentially slow) MIDI enumeration on its own thread so callers are
    // not blocked while the Windows MIDI subsystem starts up. rescan if a request
    // arrived while we were scanning
    log_misc("rawinput", "starting async MIDI scan thread");
    this->midi_thread = new std::thread([this]() {
        for (;;) {
            this->devices_scan_midi();

            // decide whether to exit under the scheduler lock, atomically with any
            // concurrent midi_scan_start(): if a rescan was requested, consume it
            // and loop; otherwise clear active and exit. because both sides take
            // the same lock, a request set while active is true is never lost, so
            // we never strand a hotplug event waiting for a future one
            std::lock_guard<std::mutex> lock(this->midi_scan_m);
            if (!this->midi_scan_pending) {
                this->midi_scan_active = false;
                log_misc("rawinput", "async MIDI scan thread finished");
                return;
            }
            this->midi_scan_pending = false;
            log_misc("rawinput", "async MIDI scan rescanning (event arrived during scan)");
        }
    });
}

void rawinput::RawInputManager::midi_scan_join() {
    if (this->midi_thread) {
        if (this->midi_thread->joinable()) {

            // this blocks until the scan worker returns. if it ever hangs here the
            // worker is stuck - most likely in midi_close_deferred_flush() waiting
            // on a WinMM close. a missing "joined" line pinpoints the hang
            log_misc("rawinput", "joining MIDI scan thread...");
            this->midi_thread->join();
            log_misc("rawinput", "MIDI scan thread joined");
        }
        delete this->midi_thread;
        this->midi_thread = nullptr;
    }
}

void rawinput::RawInputManager::midi_close_deferred_flush() {

    // take the queued handles under the lock, then close them without it. WinMM
    // midiInReset/midiInClose block until in-flight input_midi_proc callbacks
    // return, and those callbacks take devices_mutex, so closing under the lock
    // would deadlock
    std::vector<HMIDIIN> handles;
    {
        std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);
        handles.swap(this->midi_close_deferred);
    }
    if (handles.empty()) {
        return;
    }

    // if a hang is ever reported here it is the classic WinMM deadlock: an
    // in-flight input_midi_proc callback is blocked on devices_mutex while
    // midiInReset/midiInClose waits for that callback to return. the per-handle
    // log below pinpoints exactly which close did not come back
    log_misc("rawinput", "closing {} deferred MIDI handle(s)", handles.size());
    for (size_t i = 0; i < handles.size(); i++) {
        log_misc("rawinput", "closing deferred MIDI handle {}/{}", i + 1, handles.size());
        midiInReset(handles[i]);
        midiInClose(handles[i]);
    }
    log_misc("rawinput", "deferred MIDI handles closed");
}

void rawinput::RawInputManager::devices_scan_midi() {
    log_misc("rawinput", "scan MIDI devices...");

    // note: the WinMM MIDI calls below (midiInGetNumDevs / midiInGetDevCaps /
    // midiInOpen / midiInStart) can block for seconds while the Windows MIDI
    // subsystem starts up, so they must NOT run under devices_mutex. only the
    // list mutation at the end of each iteration is guarded.

    // identifiers of every MIDI device seen in this scan; used below to
    // tombstone devices that have since been unplugged
    std::vector<std::string> present_identifiers;

    // add midi devices
    auto midi_device_count = midiInGetNumDevs();
    for (size_t midi_device_id = 0; midi_device_id < midi_device_count; midi_device_id++) {

        // get dev caps
        MIDIINCAPS midi_device_caps{};
        if (midiInGetDevCaps(midi_device_id, &midi_device_caps, sizeof(MIDIINCAPS)) != MMSYSERR_NOERROR) {
            continue;
        }

        log_misc("rawinput", "found MIDI device: id {}, name {}, mid {}, pid {}",
            midi_device_id, midi_device_caps.szPname, midi_device_caps.wMid, midi_device_caps.wPid);

        // build identifier for MIDI
        // ;MIDI; format is now set in stone (in other parts of the code base and in the config xml file)
        // so it should never be changed
        std::ostringstream midi_identifier_stream;
        midi_identifier_stream << ";" << "MIDI";
        midi_identifier_stream << ";" << midi_device_id;
        midi_identifier_stream << ";" << midi_device_caps.szPname;
        midi_identifier_stream << ";" << midi_device_caps.wMid;
        midi_identifier_stream << ";" << midi_device_caps.wPid;
        const auto midi_identifier = midi_identifier_stream.str();

        // record that this device is currently present
        present_identifiers.push_back(midi_identifier);

        // if already open, leave it alone: hotplug fires many change events, and
        // reopening on every rescan would drop the WinMM handle (and its input).
        // only (re)open when the device is missing or a destroyed tombstone
        {
            std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);
            bool already_open = false;
            for (auto &device : this->devices) {
                if (device.type == MIDI && device.name == midi_identifier) {
                    already_open = true;
                    break;
                }
            }
            if (already_open) {
                continue;
            }
        }

        // open device
        HMIDIIN midi_device_handle;
        if (midiInOpen(&midi_device_handle,
                       (UINT) midi_device_id,
                       (DWORD_PTR) &input_midi_proc,
                       (DWORD_PTR) this,
                       CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        {
            continue;
        }

        // start input
        if (midiInStart(midi_device_handle) != MMSYSERR_NOERROR) {

            // close the handle we just opened so it does not leak on repeated rescans
            midiInClose(midi_device_handle);
            continue;
        }

        // device info
        DeviceInfo midi_device_info {};

        // device midi info
        auto midi_device_midi_info = new DeviceMIDIInfo();
        midi_device_midi_info->states = std::vector<bool>(16 * 128);
        midi_device_midi_info->states_events = std::vector<uint8_t>(16 * 128);
        midi_device_midi_info->bind_states = std::vector<bool>(16 * 128);
        midi_device_midi_info->v2_last_on_time = std::vector<double>(16 * 128);
        midi_device_midi_info->v2_last_off_time = std::vector<double>(16 * 128);
        midi_device_midi_info->v2_velocity_threshold = std::vector<uint8_t>(16 * 128);
        midi_device_midi_info->v2_velocity_threshold_set_on_device = std::vector<bool>(16 * 128);
        midi_device_midi_info->velocity = std::vector<uint8_t>(16 * 128);
        midi_device_midi_info->freeze = false;
        midi_device_midi_info->controls_precision = std::vector<uint16_t>(16 * 32);
        midi_device_midi_info->controls_precision_bind = std::vector<uint16_t>(16 * 32);
        midi_device_midi_info->controls_precision_msb = std::vector<bool>(16 * 32);
        midi_device_midi_info->controls_precision_lsb = std::vector<bool>(16 * 32);
        midi_device_midi_info->controls_precision_set = std::vector<bool>(16 * 32);
        midi_device_midi_info->controls_single = std::vector<uint8_t>(16 * 44);
        midi_device_midi_info->controls_single_bind = std::vector<uint8_t>(16 * 44);
        midi_device_midi_info->controls_single_set = std::vector<bool>(16 * 44);
        midi_device_midi_info->controls_onoff = std::vector<bool>(16 * 6);
        midi_device_midi_info->controls_onoff_bind = std::vector<bool>(16 * 6);
        midi_device_midi_info->controls_onoff_set = std::vector<bool>(16 * 6);
        midi_device_midi_info->v2_controls_onoff_last_on_time = std::vector<double>(16 * 6);
        midi_device_midi_info->v2_controls_onoff_last_off_time = std::vector<double>(16 * 6);
        midi_device_midi_info->pitch_bend = std::vector<int16_t>(16 * 6);
        midi_device_midi_info->pitch_bend_set = std::vector<bool>(16 * 6);

        // build device
        Device midi_device {};
        midi_device.type = MIDI;
        midi_device.handle = midi_device_handle;
        midi_device.name = midi_identifier;
        midi_device.desc = to_string(midi_device_caps.szPname);
        midi_device.info = midi_device_info;
        midi_device.mutex = new std::mutex();
        midi_device.mutex_out = new std::mutex();
        midi_device.midiInfo = midi_device_midi_info;

        // mutate the shared device list under lock (the slow WinMM calls above
        // ran without it so other threads were not blocked)
        std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);
        midi_device.id = devices.size() + 1;

        // reuse a previously destroyed tombstone with the same identifier, if any.
        // (a live device with this identifier was already skipped above)
        bool replaced = false;
        for (auto &device : this->devices) {
            if (device.name == midi_identifier) {

                // carry over ID
                midi_device.id = device.id;

                // destruct and replace, reusing the slot's existing mutexes
                this->devices_destruct(&device);
                reuse_device_mutexes(midi_device, device);
                device = midi_device;

                // notify change
                for (auto &cb : this->callback_change) {
                    cb.f(cb.data, &device);
                }

                replaced = true;
                break;
            }
        }
        if (replaced) {
            continue;
        }

        // add device to list
        auto &device = this->devices.emplace_back(midi_device);

        // notify add
        for (auto &cb : this->callback_add) {
            cb.f(cb.data, &device);
        }
    }

    // tombstone MIDI devices that were open but are no longer present (unplugged).
    // otherwise a replugged device matches the stale live entry in the skip check
    // above and never gets reopened, silently losing its input
    {
        std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);
        for (auto &device : this->devices) {
            if (device.type != MIDI) {
                continue;
            }
            bool present = false;
            for (const auto &identifier : present_identifiers) {
                if (identifier == device.name) {
                    present = true;
                    break;
                }
            }
            if (!present) {
                log_info("rawinput", "MIDI device unplugged, releasing: {}", device.desc);
                this->devices_destruct(&device);
            }
        }
    }

    // close the MIDI handles detached above, now that devices_mutex is released
    this->midi_close_deferred_flush();

    log_misc("rawinput", "scan MIDI devices done ({} enumerated)", (unsigned) midi_device_count);
}

void CALLBACK rawinput::RawInputManager::input_midi_proc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance,
                                                         DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    // get instance
    auto ri_mgr = reinterpret_cast<RawInputManager *>(dwInstance);

    // handle message
    switch (wMsg) {
        case MIM_OPEN:
        case MIM_CLOSE:
            break;
        case MIM_MOREDATA:
        case MIM_DATA: {

            // lock the device list so a concurrent scan can't mutate it while we iterate
            std::lock_guard<std::recursive_mutex> devices_lock(ri_mgr->devices_mutex);

            // param mapping
            auto dwMidiMessage = dwParam1;
            //auto dwTimestamp = dwParam2;

            // message unpacking
            auto midi_status = LOBYTE(LOWORD(dwMidiMessage));
            auto midi_status_command = (midi_status & 0xF0u) >> 4u;
            auto midi_status_channel = (midi_status & 0x0Fu);
            auto midi_byte1 = HIBYTE(LOWORD(dwMidiMessage));
            auto midi_byte2 = LOBYTE(HIWORD(dwMidiMessage));

            // callbacks
            for (auto &callback : ri_mgr->callback_midi) {

                // find device
                for (auto &device : ri_mgr->devices_get()) {
                    if (device.type == MIDI && device.handle == hMidiIn) {

                        // call function
                        callback.f(callback.data, &device,
                                   midi_status_command, midi_status_channel,
                                   midi_byte1, midi_byte2);
                    }
                }
            }

            // skip unused messages types early for performance
            bool skip = false;
            switch (midi_status_command) {
                case 0xA: // POLYPHONIC PRESSURE
                case 0xC: // PROGRAM CHANGE
                case 0xD: // CHANNEL PRESSURE
                case 0xF: // SYSTEM EXCLUSIVE
                    skip = true;
                    break;
                default:
                    break;
            }
            if (skip) {
                break;
            }

            // find device
            for (auto &device : ri_mgr->devices_get()) {

                // filter non MIDI devices
                if (device.type != MIDI) {
                    continue;
                }

                // filter wrong handles
                if (device.handle != hMidiIn) {
                    continue;
                }

                // get input time
                const auto input_time = get_performance_seconds();

                // lock device
                std::lock_guard<std::mutex> lock(*device.mutex);

                // update hz
                auto diff_time = input_time - device.input_time;
                if (diff_time > 0.0001) {
                    device.input_hz = 1.f / diff_time;
                    device.input_hz_max = MAX(device.input_hz_max, device.input_hz);
                    device.input_time = input_time;
                }

                // command logic
                switch (midi_status_command) {
                    case 0x8: { // NOTE OFF

                        // param mapping
                        const auto midi_note = midi_byte1 & 127u;

                        // log_misc("midi", "[{}] OFF", midi_note);

                        // get index
                        const auto midi_index = midi_status_channel * 128 + midi_note;
                        if (midi_index < 16 * 128) {
                            if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::LEGACY) {
                                // update velocity
                                device.midiInfo->velocity[midi_index] = 0;
                                // disable note
                                if (device.midiInfo->states_events[midi_index]) {
                                    device.midiInfo->states[midi_index] = false;
                                }
                                device.updated = true;
                            } else {
                                // v2 logic
                                // exactly the same as NOTE ON with 0 velocity
                                // velocity is kept; api will ignore it if button is not pressed
                                if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::V2) {
                                    device.midiInfo->v2_last_off_time[midi_index] = get_performance_milliseconds();
                                    device.updated = true;
                                }
                                // for v2_drum, NOTE OFF is ignored
                            }
                        }

                        break;
                    }
                    case 0x9: { // NOTE ON

                        // param mapping
                        const auto midi_note = midi_byte1 & 127u;

                        // per MIDI spec, if NOTE ON is sent with 0 velocity, it's the same thing as NOTE OFF.
                        const auto midi_velocity = midi_byte2 & 127u;

                        // log_misc("midi", "[{}] ON v={}", midi_note, midi_velocity);

                        // get index
                        const auto midi_index = midi_status_channel * 128 + midi_note;
                        if (midi_index < 16 * 128) {
                            if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::LEGACY) {
                                // update velocity
                                device.midiInfo->velocity[midi_index] = (uint8_t) midi_velocity;

                                if (midi_velocity) {
                                    // update events (for legacy logic)
                                    // how does this work? see the comment in api.cpp around the check for
                                    // get_midi_algorithm() for an explanation

                                    // so currently it's meant to be turned on
                                    device.midiInfo->states[midi_index] = true;

                                    // if its already on just increase it by one to turn it off
                                    if (device.midiInfo->states_events[midi_index] % 2)
                                        device.midiInfo->states_events[midi_index]++;
                                    else
                                        device.midiInfo->states_events[midi_index] += 2;

                                } else if (!device.midiInfo->freeze) {
                                    // velocity 0 means turn it off
                                    device.midiInfo->states[midi_index] = false;
                                }
                                device.updated = true;

                            } else {
                                // v2 logic
                                const auto now = get_performance_milliseconds();
                                auto threshold = device.midiInfo->v2_velocity_threshold[midi_index];
                                // when device is frozen (binding is happening) ignore the velocity threshold
                                // this allows users to bind keys even if the midi note is set to high threshold at
                                // rawinput layer, either from a previous binding that was cleared, or existing binding
                                // for another button
                                if (device.midiInfo->freeze) {
                                    threshold = 0;
                                }
                                if (threshold < midi_velocity) {
                                    device.midiInfo->velocity[midi_index] = (uint8_t)midi_velocity;
                                    device.midiInfo->v2_last_on_time[midi_index] = now;

                                    // disable holds and release all notes immediately
                                    if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::V2_DRUM) {
                                        device.midiInfo->v2_last_off_time[midi_index] = now;
                                    }
                                    device.updated = true;
                                } else {
                                    if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::V2) {
                                        // insufficient velocity ON == exactly the same as NOTE OFF
                                        device.midiInfo->v2_last_off_time[midi_index] = now;
                                        device.updated = true;
                                    }
                                    // for v2_drum, NOTE ON with insufficient velocity is ignored
                                }
                            }
                        }

                        break;
                    }
                    case 0xA: // POLYPHONIC PRESSURE
                        break; // skipped above (!)
                    case 0xB: { // CONTROL CHANGE

                        // param mapping
                        auto midi_control = midi_byte1 & 127;
                        auto midi_value = midi_byte2 & 127u;

                        // get index
                        auto channel_offset = midi_status_channel * 128;
                        auto midi_index = channel_offset + midi_control;
                        if (midi_index < 16 * 128) {

                            // continuous controller MSB
                            if (midi_control >= 0x00 && midi_control <= 0x1F) {

                                // update index
                                midi_index = midi_status_channel * 32 + midi_control;
                                device.midiInfo->controls_precision_set[midi_index] = true;

                                // check if MSB wasn't sent yet
                                if (!device.midiInfo->controls_precision_msb[midi_index]) {
                                    device.midiInfo->controls_precision_msb[midi_index] = true;

                                    // move LSB value to actual position
                                    device.midiInfo->controls_precision[midi_index] >>= 7u;
                                }

                                // update MSB
                                auto tmp = device.midiInfo->controls_precision[midi_index];
                                tmp = (tmp & 127u) | midi_value << 7u;
                                if (!device.midiInfo->controls_precision_lsb[midi_index])
                                    tmp = (tmp & (127u << 7u)) | midi_value;
                                if (device.midiInfo->controls_precision[midi_index] != tmp) {
                                    device.midiInfo->controls_precision[midi_index] = tmp;
                                    device.updated = true;
                                }
                            }

                            // continuous controller LSB
                            else if (midi_control >= 0x20 && midi_control <= 0x3F) {

                                // update index
                                midi_index = midi_status_channel * 32 + midi_control - 0x20;
                                device.midiInfo->controls_precision_set[midi_index] = true;
                                device.midiInfo->controls_precision_lsb[midi_index] = true;

                                // check for MSB flag
                                if (device.midiInfo->controls_precision_msb[midi_index]) {

                                    // update LSB only
                                    auto tmp = device.midiInfo->controls_precision[midi_index];
                                    tmp &= 127u << 7u;
                                    tmp |= midi_value;
                                    if (device.midiInfo->controls_precision[midi_index] != tmp) {
                                        device.midiInfo->controls_precision[midi_index] = tmp;
                                        device.updated = true;
                                    }

                                } else {

                                    // cast to MSB
                                    if (device.midiInfo->controls_precision[midi_index] != midi_value << 7u) {
                                        device.midiInfo->controls_precision[midi_index] = midi_value << 7u | midi_value;
                                        device.updated = true;
                                    }
                                }
                            }

                            // on/off controls
                            else if (midi_control >= 0x40 && midi_control <= 0x45) {

                                // update index
                                midi_index = midi_status_channel * 6 + midi_control - 0x40;
                                device.midiInfo->controls_onoff_set[midi_index] = true;

                                // get on/off state
                                const auto onoff_state = midi_value >= 64;

                                // update device
                                if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::LEGACY) {
                                    if (device.midiInfo->controls_onoff[midi_index] != onoff_state) {
                                        device.midiInfo->controls_onoff[midi_index] = onoff_state;
                                        device.updated = true;
                                    }

                                } else {
                                    // v2 and v2_drum:
                                    //   unlike notes (drum pads), controls can send continuous ON signal
                                    //   therefore, check for rising and falling edges
                                    const auto now = get_performance_milliseconds();
                                    const auto previous_value = device.midiInfo->controls_onoff[midi_index];
                                    if (!previous_value && onoff_state) {
                                        device.midiInfo->v2_controls_onoff_last_on_time[midi_index] = now;
                                        device.updated = true;
                                    } else if (previous_value && !onoff_state) {
                                        device.midiInfo->v2_controls_onoff_last_off_time[midi_index] = now;
                                        device.updated = true;
                                    }

                                    device.midiInfo->controls_onoff[midi_index] = onoff_state;
                                }
                            }

                            // single byte controllers
                            else if (midi_control >= 0x46 && midi_control <= 0x5F) {

                                // update index
                                midi_index = midi_status_channel * 44 + midi_control - 0x46;
                                device.midiInfo->controls_single_set[midi_index] = true;

                                // update device
                                if (device.midiInfo->controls_single[midi_index] != midi_value) {
                                    device.midiInfo->controls_single[midi_index] = midi_value;
                                    device.updated = true;
                                }
                            }

                            // increment/decrement and parameter numbers
                            else if (midi_control >= 0x60 && midi_control <= 0x65) {
                                // skip
                            }

                            // undefined single-byte controllers
                            else if (midi_control >= 0x66 && midi_control <= 0x77) {

                                // update index
                                auto sbc_count = 0x5F - 0x46 + 1;
                                midi_index = midi_status_channel * 44 + midi_control - 0x66 + sbc_count;
                                device.midiInfo->controls_single_set[midi_index] = true;

                                // update device
                                if (device.midiInfo->controls_single[midi_index] != midi_value) {
                                    device.midiInfo->controls_single[midi_index] = midi_value;
                                    device.updated = true;
                                }
                            }

                            // channel mode messages
                            else if (midi_control >= 0x78 && midi_control <= 0x7F) {
                                switch (midi_control) {
                                    case 0x78: // all sound off
                                        break;
                                    case 0x79: { // reset all controllers
                                        for (int i = 0; i < 32; i++)
                                            device.midiInfo->controls_precision[midi_status_channel * 32 + i] = 0;
                                        for (int i = 0; i < 44; i++)
                                            device.midiInfo->controls_single[midi_status_channel * 44 + i] = 0;
                                        for (int i = 0; i < 6; i++) {
                                            const auto index = midi_status_channel * 6 + i;
                                            device.midiInfo->controls_onoff[index] = false;
                                            device.midiInfo->v2_controls_onoff_last_on_time[index] = 0;
                                            device.midiInfo->v2_controls_onoff_last_off_time[index] = 0;
                                        }
                                        device.updated = true;
                                        break;
                                    }
                                    case 0x7A: // local control on/off
                                        break;
                                    case 0x7B: // all notes off
                                    case 0x7C: // omni mode off + all notes off
                                    case 0x7D: // omni mode on + all notes off
                                    case 0x7E: // mono mode on + poly off + all notes off
                                    case 0x7F: // poly mode on + mono off + all notes off
                                        for (int i = 0; i < 128; i++) {
                                            // common
                                            device.midiInfo->velocity[channel_offset + i] = 0;
                                            device.midiInfo->bind_states[channel_offset + i] = false;

                                            // legacy
                                            device.midiInfo->states[channel_offset + i] = false;
                                            device.midiInfo->states_events[channel_offset + i] = 0;

                                            // v2
                                            device.midiInfo->v2_last_off_time[channel_offset + i] = 0.0;
                                            device.midiInfo->v2_last_on_time[channel_offset + i] = 0.0;
                                        }
                                        device.updated = true;
                                        break;
                                    default:
                                        break;
                                }
                                break;
                            }
                        }
                        break;
                    }
                    case 0xC: // PROGRAM CHANGE
                        break; // skipped above (!)
                    case 0xD: // CHANNEL PRESSURE
                        break; // skipped above (!)
                    case 0xE: { // PITCH BENDING

                        // raw values range from [0, 0x3FFF] (16383)
                        // build value, centered around zero [-8192, 8191]
                        int16_t value = ((midi_byte1) | (midi_byte2 << 7u)) - 0x2000;

                        // update device
                        if (device.midiInfo->pitch_bend[midi_status_channel] != value) {
                            device.midiInfo->pitch_bend[midi_status_channel] = value;
                            device.midiInfo->pitch_bend_set[midi_status_channel] = true;
                            device.updated = true;
                        }
                        break;
                    }
                    case 0xF: // SYSTEM EXCLUSIVE
                        break; // skipped above (!)
                    default:
                        break;
                }

                // don't iterate through the other devices
                break;
            }
            break;
        }
        case MIM_LONGDATA:
        case MIM_ERROR:
        case MIM_LONGERROR:
            break;
        default:
            break;
    }
}
