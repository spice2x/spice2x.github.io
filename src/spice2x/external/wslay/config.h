/* Hand written stand-in for the autotools/CMake generated config.h. Spice only
   builds this for Windows on x86, so the probes have single known answers. */
#ifndef CONFIG_H
#define CONFIG_H

#define HAVE_WINSOCK2_H
/* x86 and x86_64 are little endian, so WORDS_BIGENDIAN stays undefined */

#endif /* CONFIG_H */
