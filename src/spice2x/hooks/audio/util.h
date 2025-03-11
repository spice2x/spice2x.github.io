#pragma once

#include <string>

#include <windows.h>
#include <audiosessiontypes.h>
#include <mmreg.h>

std::string channel_mask_str(DWORD channel_mask);
std::string share_mode_str(AUDCLNT_SHAREMODE share_mode);
void copy_wave_format(WAVEFORMATEXTENSIBLE *destination, const WAVEFORMATEX *source);
void print_format(const WAVEFORMATEX *pFormat);
