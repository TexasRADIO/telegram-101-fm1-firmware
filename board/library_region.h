/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ELITE_LIBRARY_REGION_H
#define ELITE_LIBRARY_REGION_H
/* Historical backup shows these sectors erased. Runtime ownership and chip
 * checks are still mandatory; this constant alone never authorizes a write. */
#define ELITE_LIBRARY_LO 0x000A0000u
#define ELITE_LIBRARY_HI 0x000A8000u
#if defined(BOARD_FM1) && !(defined(BOARD_SMK37) && BOARD_SMK37) /* (the host tests model the SMK map) */
/* The FM-1's flash map (fm1_flash_map.h, pinned) has its data windows but not the SMK-37's window checks. The
 * same checks for the FM-1: the head (SPL, isd_config) below 0x4000 is never written, and a library window must
 * be whole sectors inside the data VM (0x97000..0xE0000), clear of the OTA staging, BTIF and the stock USR area. */
#define FL_HEAD_HI 0x00004000u
#define FL_WIN_SAFE(lo, hi) ((lo) % 0x1000u == 0u && (hi) % 0x1000u == 0u && (lo) < (hi) && \
                             (lo) >= FL_DATA_LO && (hi) <= FL_DATA_HI)
#endif
#endif
