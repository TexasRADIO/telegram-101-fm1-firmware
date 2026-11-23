/* SPDX-License-Identifier: GPL-3.0-only */
/* Telegram 101 on the M-VAVE FM-1: one scan of the panel, and the way into USB update mode.
 *
 * Included after panel101.inc, fm1_front.inc and instrument_view.inc. main.c calls fm1_board_scan()
 * once a main-loop pass, before read_performance(): it reads the FM-1's buttons, knobs and keys,
 * hands them to fm1_front_run(), and leaves the result where the panel reads its input
 * (fm1_input_shim.h).
 *
 * The BB Recovery Gesture (BB Install Standard 3.4), timed and worded as BB1's (BB1 main.c, ui_draw.c
 * draw_uboot): hold OCT- and OCT+ together. From 2 s a countdown fills the screen, entry at 5 s; while held, nothing else on the panel does anything (no notes, no pages, no writes).
 * Let go before it ends and the instrument is as it was. At the end the app stops the audio, tells the
 * boot guard the restart is intended, detaches USB and enters the chip's USB boot (WL80UBOOT,
 * 4C4A:8057), the route M-VAVE's updater and the Big Beat installer use. Nothing is written by the
 * gesture itself. It needs the app to be running; the boot stub's rescue key (OCT- held at power-on)
 * is the route that does not. */

enum { FM1_RESCUE_MS = 5000u, FM1_RESCUE_SHOW_MS = 2000u }; /* BB1's: held 5 s, the countdown from 2 s */
static struct {
    uint32_t since;
    uint8_t held;
    uint8_t left; /* seconds left, shown; 0 = no countdown on screen */
} fm1_rescue;

/* One scan, without the hardware: `held` and `pressed` in FM-1 matrix ids, `enc` the clicks since the
 * last scan (enum fm1_encoder), `notes` the 27 keys. Fills `out` with what the panel gets this scan.
 * Returns 1 when the recovery gesture has been held to the end. */
static unsigned fm1_board_apply(struct panel *panel, uint32_t now_ms, uint32_t held, uint32_t pressed,
                                const int enc[F1_ENCODERS], uint32_t notes, struct fm1_virtual *out)
{
    const uint32_t both = (1u << F1_OCT_DOWN) | (1u << F1_OCT_UP);
    memset(out, 0, sizeof *out);
    if ((held & both) == both) {
        if (!fm1_rescue.held) { fm1_rescue.held = 1; fm1_rescue.since = now_ms; }
        uint32_t elapsed = now_ms - fm1_rescue.since;
        /* The panel sees nothing while the gesture is held: keys up, no buttons, no turns. */
        fm1_rescue.left = elapsed < FM1_RESCUE_SHOW_MS ? 0 :
            (uint8_t)((FM1_RESCUE_MS - (elapsed < FM1_RESCUE_MS ? elapsed : FM1_RESCUE_MS) + 999u) / 1000u);
        return elapsed >= FM1_RESCUE_MS;
    }
    if (fm1_rescue.held) {
        /* Let go before the end: cancelled, and nothing reaches the panel until both octave buttons
         * are up, so letting go does not also shift the octave. */
        if (held & both) return 0;
        fm1_rescue.held = 0;
        fm1_rescue.left = 0;
        pressed = 0;
    }

    /* No pedal, no TRS jack on the FM-1: the settings that would use them stay off. */
    panel->pedal_mode = PEDAL_OFF;
    panel->midi_tx &= (uint8_t)~TG_MIDI_ROUTE_TRS;
    fm1_front_run(panel, &fm1_front_state, held, pressed, enc, notes, out);
    return 0;
}

#ifndef FM1_BOARD_HOST
/* One scan of the FM-1 (main.c, once a main-loop pass, before read_performance). */
static unsigned fm1_board_scan(struct panel *panel)
{
    fm1_shim_clock();
    uint32_t irq = fm1_icfg();
    fm1_irq_off();
    uint32_t held = fm1_hw.buttons, notes = fm1_hw.notes;
    fm1_icfg_set(irq);
    uint32_t released = 0, pressed = fm1_hw_input_edges(&released);
    int enc[F1_ENCODERS];
    for (unsigned e = 0; e < F1_ENCODERS; e++) enc[e] = (int)fm1_hw_enc_take(e);
    (void)released;
    struct fm1_virtual v;
    unsigned done = fm1_board_apply(panel, fm1_ms, held, pressed, enc, notes, &v);
    fm1_in.keys = v.keys;
    fm1_in.buttons = v.buttons;
    fm1_shim_pressed |= v.pressed;
    for (unsigned k = 0; k < 8; k++) fm1_shim_knob[k] += v.knob[k];
    fm1_shim_pads(v.pads);
    return done;
}
#endif

#ifndef FM1_BOARD_NO_VIEW
/* The countdown card, over everything else, worded as BB1's. `waiting`: the gesture or the update-mode key
 * has been answered and the app is letting go (BB1 shows "UBOOT"). */
static void fm1_rescue_draw(uint16_t *pixels, unsigned waiting)
{
    view_rect(pixels, 0, 0, VIEW_W, VIEW_H, CHROME_BAND);
    if (waiting) {
        view_title(pixels, 88, 104, "UBOOT", CHROME_ELITE);
        view_small(pixels, (VIEW_W - view_small_width("WAITING FOR THE INSTALLER")) / 2, 150, "WAITING FOR THE INSTALLER", CHROME_CREAM);
        view_small(pixels, (VIEW_W - view_small_width("POWER OFF AND ON TO PLAY")) / 2, 170, "POWER OFF AND ON TO PLAY", CHROME_CREAM);
        return;
    }
    char digit[2] = {(char)('0' + (fm1_rescue.left > 9 ? 9 : fm1_rescue.left)), 0};
    view_small(pixels, (VIEW_W - view_small_width("UPDATE MODE IN")) / 2, 80, "UPDATE MODE IN", CHROME_CREAM);
    view_title(pixels, (VIEW_W - 22) / 2, 100, digit, CHROME_ELITE);
    view_small(pixels, (VIEW_W - view_small_width("OCT- OCT+  LET GO TO CANCEL")) / 2, 152, "OCT- OCT+  LET GO TO CANCEL", CHROME_CREAM);
}
#endif /* FM1_BOARD_NO_VIEW */
