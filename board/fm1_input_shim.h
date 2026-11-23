/* SPDX-License-Identifier: GPL-3.0-only */
/* Telegram 101 on the M-VAVE FM-1: the input layer the panel is given.
 *
 * The panel (panel101.inc) was written against the SMK-37's input (smk_input.h): 37 keys, 18 buttons,
 * eight knobs, pads, faders and wheels. On the FM-1 the real hardware layer is fm1_input.h. This file
 * includes that layer under its own names (fm1_hw, fm1_hw_enc_take, ...) and provides, under the
 * names the panel reads, an SMK-37-shaped input that fm1_front.inc fills once a scan:
 *
 *   fm1_in.keys / buttons / frames   the 27 keys as keys 5..31, the virtual buttons, a 625 Hz clock
 *   fm1_input_edges()                the virtual button presses of the last scan
 *   fm1_enc_take(k)                  the virtual knob turns of the last scan (k = SMK-37 knob 0..7)
 *   fm1_led_key(id, on)              an SMK-37 button id, lit on the FM-1 button printed for it
 *
 * The FM-1 has no pads, faders, wheels, pedal or TRS MIDI out: those read as idle (wheel centred,
 * pedal released, no pad events) and their outputs do nothing. */
#ifndef FM1_INPUT_SHIM_H
#define FM1_INPUT_SHIM_H

#define fm1_in fm1_hw
#define fm1_enc_take fm1_hw_enc_take
#define fm1_input_edges fm1_hw_input_edges
#define fm1_input_note_edges fm1_hw_input_note_edges
#define fm1_led_key fm1_hw_led_key
#define FM1_BTN_OCT_DOWN FM1_HW_BTN_OCT_DOWN
#define FM1_BTN_OCT_UP FM1_HW_BTN_OCT_UP
#include "fm1_input.h"
#include "fm1_adc.h"
#undef fm1_in
#undef fm1_enc_take
#undef fm1_input_edges
#undef fm1_input_note_edges
#undef fm1_led_key
#undef FM1_BTN_OCT_DOWN
#undef FM1_BTN_OCT_UP

/* The SMK-37 ids the panel uses (smk_input.h). */
enum { FM1_BTN_OCT_DOWN = 4, FM1_BTN_OCT_UP = 5 };
enum { SMK_BTN_KNOB_BANK = 0, SMK_BTN_FADER_BANK = 1, SMK_BTN_LEFT = 2, SMK_BTN_RIGHT = 3 };
#define SMK_NPAD 16u
#define SMK_PADQ 16u

/* The FM-1's own matrix ids (BB1 panel.c PANEL_DEFAULT, measured on hardware). */
enum {
    FM1_HW_OCT_DOWN = 0, FM1_HW_OCT_UP = 1, FM1_HW_FX = 2, FM1_HW_SCL = 3, FM1_HW_ENV = 4, FM1_HW_LFO = 5,
    FM1_HW_EDIT = 6, FM1_HW_GLO = 7, FM1_HW_HOME = 8, FM1_HW_SAVE = 9, FM1_HW_ARP = 10, FM1_HW_SEQ = 11,
    FM1_HW_PLAY = 12, FM1_HW_REC = 13
};

static volatile uint32_t fm1_ms; /* main.c's millisecond count (the same tentative definition) */

static struct {
    uint64_t keys;               /* the 37-key view: the FM-1's 27 keys are keys 5..31 */
    uint32_t buttons;            /* SMK-37 button ids held */
    uint32_t frames;             /* 625 a second, as the SMK-37's scan counts them */
    uint8_t key_vel[64];         /* the FM-1's keys have no velocity: a fixed one */
    uint8_t fader[4];            /* no faders: never move, so never pick up */
    uint16_t wheel[2], pedal;    /* bend centred, no modulation, pedal released (pedal_mode is kept OFF) */
} fm1_in;

static uint32_t fm1_shim_pressed;   /* SMK-37 button presses not yet taken by the panel */
static int fm1_shim_knob[8];        /* SMK-37 knob turns not yet taken by the panel */
static uint32_t fm1_shim_ms, fm1_shim_rest; /* the 625 Hz clock from fm1_ms */

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t pressed = fm1_shim_pressed;
    fm1_shim_pressed = 0;
    if (released) *released = 0;
    return pressed;
}

static int32_t fm1_enc_take(uint32_t knob)
{
    if (knob >= 8u) return 0;
    int turn = fm1_shim_knob[knob];
    fm1_shim_knob[knob] = 0;
    return turn;
}

/* An SMK-37 button's light on the FM-1 button that stands for it; none where the FM-1 has none. */
static void fm1_led_key(uint32_t smk_id, int on)
{
    static const int8_t to_fm1[18] = {
        [0] = -1, [SMK_BTN_FADER_BANK] = FM1_HW_EDIT, [2] = -1, [3] = -1,
        [FM1_BTN_OCT_DOWN] = FM1_HW_OCT_DOWN, [FM1_BTN_OCT_UP] = FM1_HW_OCT_UP,
        [6] = FM1_HW_ARP, [7] = -1, [8] = FM1_HW_SCL, [9] = -1, [10] = FM1_HW_GLO, [11] = FM1_HW_HOME,
        [12] = FM1_HW_SAVE, [13] = -1, [14] = FM1_HW_FX, [15] = FM1_HW_SEQ, [16] = FM1_HW_PLAY, [17] = FM1_HW_REC,
    };
    if (smk_id < 18u && to_fm1[smk_id] >= 0) fm1_hw_led_key((uint32_t)to_fm1[smk_id], on);
}

/* The 625 Hz frame clock the panel's timings are written in. */
static void fm1_shim_clock(void)
{
    uint32_t now = fm1_ms, elapsed = now - fm1_shim_ms;
    fm1_shim_ms = now;
    if (elapsed > 1000u) elapsed = 1000u; /* a stall is not a second of held buttons */
    fm1_shim_rest += elapsed * 5u;
    fm1_in.frames += fm1_shim_rest / 8u;
    fm1_shim_rest %= 8u;
}

static void fm1_shim_init(void)
{
    memset(&fm1_in, 0, sizeof fm1_in);
    memset(fm1_in.key_vel, 100, sizeof fm1_in.key_vel);
    fm1_in.wheel[0] = 512;
    fm1_in.frames = 1;
    fm1_shim_ms = fm1_ms;
}

/* No pads on the FM-1. On the library page its 16 white keys stand in for them (fm1_front.inc): their presses
 * and releases arrive here as the 37's pad events do (pad << 8 | velocity, 0 = release). */
static uint16_t smk_pad_own;
static volatile uint8_t smk_padq_w, smk_padq_r;
static int32_t fm1_shim_padq[SMK_PADQ];
static const int8_t *volatile smk_pad_map;
static void fm1_shim_pads(uint16_t pads)
{
    uint16_t changed = (uint16_t)(pads ^ smk_pad_own);
    for (unsigned pad = 0; pad < SMK_NPAD; pad++) {
        if (!((changed >> pad) & 1u)) continue;
        uint8_t w = smk_padq_w;
        if ((uint8_t)((w + 1u) % SMK_PADQ) == smk_padq_r) break; /* full: the rest wait for the next scan */
        fm1_shim_padq[w] = (int32_t)(pad << 8 | ((pads >> pad) & 1u ? 100u : 0u));
        smk_padq_w = (uint8_t)((w + 1u) % SMK_PADQ);
        smk_pad_own = (uint16_t)(smk_pad_own ^ (1u << pad));
    }
}
static int32_t smk_pad_take(void)
{
    if (smk_padq_r == smk_padq_w) return -1;
    int32_t event = fm1_shim_padq[smk_padq_r];
    smk_padq_r = (uint8_t)((smk_padq_r + 1u) % SMK_PADQ);
    return event;
}
static int32_t smk_extkey_take(void) { return -1; }
static uint8_t padled_dirty;
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b) { (void)pad; (void)r; (void)g; (void)b; }
static void padled_flush(void) { padled_dirty = 0; }

/* ---------- MASTER: the FM-1's pot is Telegram's MAIN ----------
 * Read as BB1 reads it (master.c): before the audio starts, then polled from the main loop with the
 * scan's converter left idle. A move of the pot sets MAIN; MAIN's knob on home also moves it, and the
 * last one touched wins, as on a desk with a fader and a remote. */
static int32_t fm1_master_x16 = 512 * 16;
static int16_t fm1_master_sent = -1;

static int fm1_master_boot(void)
{
    (void)fm1_adc_read(FM1_ADC_MASTER); /* the first conversion after the pin setup: dropped */
    for (unsigned i = 0; i < 3u; i++) {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0) { fm1_master_x16 = a * 16; return (int)(a * 127 / 1023); }
    }
    return -1;
}

/* MAIN's new value when the pot has moved by at least a step since it last set MAIN; -1 otherwise. */
static int fm1_master_poll(void)
{
    int32_t a = fm1_adc_read(FM1_ADC_MASTER);
    if (a < 0) return -1;
    fm1_master_x16 += (a * 16 - fm1_master_x16) / 8;
    int level = (int)((fm1_master_x16 / 16) * 127 / 1023);
    if (fm1_master_sent >= 0 && level >= fm1_master_sent - 1 && level <= fm1_master_sent + 1) return -1;
    fm1_master_sent = (int16_t)level;
    return level;
}

#endif /* FM1_INPUT_SHIM_H */
