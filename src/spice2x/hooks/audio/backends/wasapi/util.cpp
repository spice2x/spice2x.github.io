#include "util.h"

#include <audioclient.h>

#include "util/flags_helper.h"

#include "defs.h"

std::string stream_flags_str(DWORD flags) {
    FLAGS_START(flags);
    FLAG(flags, AUDCLNT_STREAMFLAGS_CROSSPROCESS);
    FLAG(flags, AUDCLNT_STREAMFLAGS_LOOPBACK);
    FLAG(flags, AUDCLNT_STREAMFLAGS_EVENTCALLBACK);
    FLAG(flags, AUDCLNT_STREAMFLAGS_NOPERSIST);
    FLAG(flags, AUDCLNT_STREAMFLAGS_RATEADJUST);
    FLAG(flags, AUDCLNT_STREAMFLAGS_PREVENT_LOOPBACK_CAPTURE);
    FLAG(flags, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM);
    FLAG(flags, AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
    FLAGS_END(flags);
}
