/* SPDX-License-Identifier: GPL-3.0-only */
/* Telegram 101 on the M-VAVE FM-1: the front panel.
 *
 * The FM-1 has seven endless knobs (PRESETS, SELECT, ALGORITHM, KNOB 1-4), a MASTER pot, fourteen
 * buttons and 27 keys. It has no pads, faders or wheels. This file turns those controls into the
 * moves the Telegram panel already understands, so the FM-1 plays the same instrument as the
 * SMK-37: the same engine, sounds, sequencer, arp and screens.
 *
 * The layout follows the instrument the FM-1's panel was printed for:
 *   PRESETS      the next or previous sound, from any page, and back to home
 *   ALGORITHM    on home, the next or previous sound bank; elsewhere, the page's type switch
 *   SELECT       turns the pages of the part on screen
 *   KNOB 1-4     the four controls drawn along the bottom of the screen
 *   HOME         home; from home, the Band
 *   EDIT         Shape: SOURCE MIX, then FILTER, ENVELOPE, MODULATOR on each further press
 *   ENV          ENVELOPE; again for FILTER ENV
 *   LFO          MODULATOR
 *   FX ARP SEQ PLAY REC OCT- OCT+   as printed
 *   GLO          Global (and the shift for chords)
 *   SCL          key transpose
 *   SAVE         the sound library (its 16 slots on the 16 white keys)
 *
 * Included after panel101.inc. It reads FM-1 ids and writes a virtual SMK-37 input, which the
 * caller hands to read_controls(). */

#ifndef FM1_FRONT_INC
#define FM1_FRONT_INC

enum fm1_button {
    F1_OCT_DOWN, F1_OCT_UP, F1_FX, F1_SCL, F1_ENV, F1_LFO, F1_EDIT, F1_GLO,
    F1_HOME, F1_SAVE, F1_ARP, F1_SEQ, F1_PLAY, F1_REC, F1_BUTTONS
};
/* The matrix encoder behind each printed knob (measured on hardware for BB1). */
enum fm1_encoder { F1E_SELECT, F1E_ALGORITHM, F1E_K1, F1E_K2, F1E_K3, F1E_K4, F1E_PRESETS, F1_ENCODERS };
enum { F1_KEYS = 27, F1_KEY_SHIFT = 5, F1_HOME_PAGES = 4 }; /* the FM-1's low F sits five keys above the 37's low C */

struct fm1_front {
    uint8_t home_page; /* which four home controls KNOB 1-4 hold */
    uint8_t half;      /* on an eight-knob page, which four */
    uint8_t pads;      /* GLO + SEQ: the white keys are the 37's 16 pads on every page, until GLO + SEQ or HOME */
    uint8_t glo_used;  /* GLO made a chord: its release opens nothing, and the 37 never sees it held */
    uint8_t repeat;    /* GLO + REC: NOTE REPEAT, held for as long as REC is */
    uint8_t tie;       /* GLO + PLAY: CHORD/HOLD (the 37's TIE), held for as long as PLAY is */
    uint8_t keys;      /* GLO + SCL: the keys handoff, held for as long as SCL is */
};

static struct fm1_front fm1_front_state; /* one panel; the screen reads it to draw the deck */

/* What the SMK-37 panel code is given in place of its own inputs. */
struct fm1_virtual {
    uint64_t keys;
    uint32_t buttons, pressed;
    int knob[8];
    uint16_t pads; /* the white keys standing in for the 37's pads on this page (see fm1_front_white_pads) */
};

/* The SMK-37 has 16 pads; the FM-1 has exactly 16 white keys, low F to high G. On the library page they are
 * pads 1-16 and play no notes, as on every library shelf. GLO + SEQ makes them the pads on every page (steps, drums, the Band's parts),
 * until GLO + SEQ again or HOME. Everywhere else the keys play. */
static unsigned fm1_front_white_pads(const struct panel *panel, const struct fm1_front *front)
{
#ifdef ELITE_DRUMS808
    if (library_active(panel)) return 1; /* every library shelf (sounds, patterns, songs) picks with the pads */
#endif
    return panel->page == PAGE_PATCH || front->pads;
}

/* Home's four knob pages. MAIN is here until the MASTER pot is read (see docs/FM1-PORT.md). */
static const struct { uint8_t bank, knob[4]; } fm1_home_deck[F1_HOME_PAGES] = {
    {0, {1, 2, 3, 5}}, /* CUTOFF  RESONANCE  ENV  DECAY */
    {0, {4, 6, 7, 0}}, /* WIDTH  LFO RATE  GLIDE  MAIN */
    {1, {1, 2, 3, 4}}, /* TRACK  PWM  FILTER LFO  VIBRATO */
    {1, {5, 6, 7, 0}}, /* OSC OCTAVE  TEMPO  GLIDE  MAIN */
};

/* Pages whose eight SMK-37 knobs are shown four at a time, SELECT turning the half. */
static unsigned fm1_front_halves(const struct panel *panel)
{
    return panel->page == PAGE_ARP || panel->page == PAGE_GLOBAL;
}

/* Shape has no knob row of its own on the FM-1: KNOB 1-4 are the 101's four sliders. */
static unsigned fm1_front_sliders(const struct panel *panel)
{
    return panel->page == PAGE_MANUAL && !panel->save_mode;
}

/* The SMK-37 knob each of KNOB 1-4 stands for on this page. */
static void fm1_front_deck(const struct panel *panel, const struct fm1_front *front, uint8_t knob[4])
{
    static const uint8_t first[4] = {0, 1, 2, 3}, second[4] = {4, 5, 6, 7};
    static const uint8_t steps[4] = {7, 5, 6, 0}; /* STEP/PITCH  GATE  LENGTH  MAIN */
    static const uint8_t band[4] = {0, 1, 2, 4};  /* 808  303  101 levels  BPM (MASTER is the main level) */
    const uint8_t *from = first;
    if (panel->page == PAGE_TONE) from = fm1_home_deck[front->home_page % F1_HOME_PAGES].knob;
    else if (panel->page == PAGE_STEPS) from = steps;
    else if (panel->page == PAGE_CONDUCTOR) from = band;
    else if (fm1_front_halves(panel) && front->half) from = second;
    memcpy(knob, from, 4);
}

/* The SMK-37 knob ALGORITHM stands for: the stepped "type" control of the page on screen. */
static int fm1_front_type_knob(const struct panel *panel)
{
    switch (panel->page) {
    case PAGE_MANUAL: return panel->fader_bank == 0 ? 7 : panel->fader_bank == 1 ? -1 : 6; /* SUB, -, TRIGGER, LFO WAVE */
    case PAGE_FILTER_ENV: return 0; /* SHARED, OPENS, CLOSES */
    case PAGE_ARP: return 0;        /* the arp's mode */
    case PAGE_FX: return 3;         /* glide mode */
    case PAGE_STEPS: return 7;
    case PAGE_PATCH: return 7;      /* the sound family */
    default: return -1;
    }
}

static void fm1_front_shape(struct panel *panel, unsigned section)
{
    panel->fader_bank = (uint8_t)(section % FADER_PAGES);
    panel->page = PAGE_MANUAL;
    panel->record = 0; /* leaving entry must not keep writing unseen steps */
    panel->save_mode = SAVE_NONE;
}

/* One scan of the FM-1's controls. `buttons` and `pressed` use enum fm1_button, `enc` holds each
 * encoder's clicks since the last call (+ clockwise), `notes` the 27 keys from the low F. */
static void fm1_front_run(struct panel *panel, struct fm1_front *front, uint32_t buttons, uint32_t pressed,
                          const int enc[F1_ENCODERS], uint32_t notes, struct fm1_virtual *out)
{
    static const uint8_t held_map[F1_BUTTONS] = {
        [F1_OCT_DOWN] = FM1_BTN_OCT_DOWN, [F1_OCT_UP] = FM1_BTN_OCT_UP, [F1_FX] = BTN_FX, [F1_SCL] = BTN_SCALE,
        [F1_GLO] = BTN_GLOBE, [F1_ARP] = BTN_ARP, [F1_SEQ] = BTN_SEQ, [F1_PLAY] = BTN_PLAY, [F1_REC] = BTN_REC,
        [F1_SAVE] = BTN_PATCH, [F1_HOME] = BTN_ASSIGN, [F1_EDIT] = SMK_BTN_FADER_BANK,
    };
    /* HOME is a tap only: held, the SMK-37's BT opens knob assignment, which the FM-1 has no use for.
     * ENV and LFO are handled here and never reach the SMK-37 panel. */
    const uint32_t tap_only = 1u << F1_HOME, local = (1u << F1_ENV) | (1u << F1_LFO);
    memset(out, 0, sizeof *out);

    /* GLO chords for the 37 buttons the FM-1 has no key for. Each fires on the second button's press:
     *   GLO + PLAY  HOLD/CHORD (the 37's TIE; on ARP, the arp latch), held while PLAY is
 *   GLO + REC   NOTE REPEAT (the 37's REST), held while REC is
     *   GLO + SCL   the keys handoff, Telegram/Dillo (GLOBE + KNOB BANK)
     *   GLO + LFO   INFO (the 37's PARA)                            GLO + SEQ   the white keys become pads
     * The second button does nothing else, and GLO's own tap (Global) is cancelled. */
    unsigned glo = buttons & (1u << F1_GLO);
    uint32_t chord_para = 0;
    if (!glo && !(buttons & (1u << F1_SCL))) front->keys = 0;
    if (!(buttons & (1u << F1_REC))) front->repeat = 0;
    if (!(buttons & (1u << F1_PLAY))) front->tie = 0;
    if (glo) {
        if (pressed & (1u << F1_PLAY)) { front->tie = 1; front->glo_used = 1; out->pressed |= 1u << BTN_TIE; }
        if (pressed & (1u << F1_REC)) { front->repeat = 1; front->glo_used = 1; out->pressed |= 1u << BTN_REST; }
        if (pressed & (1u << F1_SCL)) { front->keys = 1; out->pressed |= 1u << SMK_BTN_KNOB_BANK; }
        if (pressed & (1u << F1_LFO)) { chord_para = 1; front->glo_used = 1; }
        if (pressed & (1u << F1_SEQ)) { front->pads = (uint8_t)!front->pads; front->glo_used = 1; }
    }
    if (front->glo_used || front->keys) panel->globe_tap_pending = 0;
    uint32_t chorded = 0;
    if (glo || front->glo_used) chorded |= (1u << F1_LFO) | (1u << F1_SEQ);
    if (glo || front->tie) chorded |= 1u << F1_PLAY;
    if (front->repeat) chorded |= 1u << F1_REC;
    if (front->keys) chorded |= 1u << F1_SCL;
    if (glo) chorded |= (1u << F1_REC) | (1u << F1_SCL);
    if (front->repeat) out->buttons |= 1u << BTN_REST;
    if (front->keys) out->buttons |= 1u << SMK_BTN_KNOB_BANK;
    if (front->tie) out->buttons |= 1u << BTN_TIE;
    if (chord_para) { out->pressed |= 1u << BTN_PARA; out->buttons |= 1u << BTN_PARA; }
    if (!glo) front->glo_used = 0;
    if (pressed & (1u << F1_HOME)) front->pads = 0; /* HOME: back to playing notes */
    buttons &= ~chorded;
    pressed &= ~chorded;
    if (front->glo_used && !front->keys) buttons &= ~(1u << F1_GLO); /* the 37 sees the chord's button alone */

    if (fm1_front_white_pads(panel, front)) {
        static const uint16_t black = (1u << 1) | (1u << 3) | (1u << 6) | (1u << 8) | (1u << 10);
        for (unsigned n = 0, white = 0; n < F1_KEYS; n++) {
            if ((black >> ((n + F1_KEY_SHIFT) % 12u)) & 1u) continue;
            if (notes & (1u << n)) out->pads |= (uint16_t)(1u << white);
            white++;
        }
        notes = 0; /* the black keys are quiet here too: one gesture per key */
    }
    out->keys = (uint64_t)(notes & ((1u << F1_KEYS) - 1u)) << F1_KEY_SHIFT;
    for (unsigned b = 0; b < F1_BUTTONS; b++) {
        if (local & (1u << b)) continue;
        if ((buttons & (1u << b)) && !(tap_only & (1u << b))) out->buttons |= 1u << held_map[b];
        if (pressed & (1u << b)) out->pressed |= 1u << held_map[b];
    }

    unsigned library = panel->page == PAGE_PATCH || panel->save_mode;
#ifdef ELITE_DRUMS808
    library = library || library_active(panel);
#endif
    if (pressed & (1u << F1_ENV)) {
        if (panel->page == PAGE_MANUAL && panel->fader_bank == 2) panel->page = PAGE_FILTER_ENV;
        else fm1_front_shape(panel, 2);
    }
    if (pressed & (1u << F1_LFO)) fm1_front_shape(panel, 3);

    /* PRESETS: the sounds, one a click, from any page; it always lands on home. */
    if (enc[F1E_PRESETS] && !library) {
        telegram_browse(panel, enc[F1E_PRESETS], 1);
        panel->page = PAGE_TONE;
        panel->record = 0;
    }

    /* ALGORITHM: on home the bank, elsewhere the page's type switch. */
    if (enc[F1E_ALGORITHM]) {
        int type = fm1_front_type_knob(panel);
        if (panel->page == PAGE_TONE) telegram_browse(panel, enc[F1E_ALGORITHM] * (int)TG_PADS, 1);
        else if (type >= 0) out->knob[type] += enc[F1E_ALGORITHM];
    }

    /* SELECT: the pages of the part on screen. */
    if (enc[F1E_SELECT]) {
        int turn = enc[F1E_SELECT];
        if (panel->page == PAGE_TONE) {
            front->home_page = (uint8_t)(((int)front->home_page + turn % F1_HOME_PAGES + F1_HOME_PAGES) % F1_HOME_PAGES);
        } else if (fm1_front_halves(panel)) {
            front->half = turn > 0;
        } else {
            out->pressed |= 1u << (turn > 0 ? SMK_BTN_RIGHT : SMK_BTN_LEFT); /* sections, step pages, banks */
        }
    }
    if (panel->page == PAGE_TONE) panel->knob_bank = fm1_home_deck[front->home_page % F1_HOME_PAGES].bank;
    else if (!fm1_front_halves(panel)) front->half = 0;

    /* KNOB 1-4: the deck on screen. */
    uint8_t deck[4];
    fm1_front_deck(panel, front, deck);
    for (unsigned k = 0; k < 4; k++) {
        int turn = enc[F1E_K1 + k];
        if (!turn) continue;
        if (fm1_front_sliders(panel)) {
            /* A knob has no position to catch up with, so it moves the slider's value at once. */
            unsigned control = panel->fader_map[panel->fader_bank][k];
            set_control(&panel->sound, control, (int)control_value(&panel->sound, control) + knob_speed(turn) * 2);
        } else {
            out->knob[deck[k]] += turn;
        }
    }
}

/* (the firmware has no strcmp) */
static int fm1_front_same(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* The name printed on the FM-1 for a hint written with the SMK-37's key names; NULL where the FM-1
 * has no such control, so the hint is left out rather than naming a key that is not there. */
static const char *fm1_front_hint_key(const struct panel *panel, const char *key, const char *action)
{
    static const char *const same[] = {"FX", "ARP", "SEQ", "REC", "PLAY"};
    for (unsigned i = 0; i < sizeof same / sizeof same[0]; i++) if (fm1_front_same(key, same[i])) return key;
    if (fm1_front_same(key, "< >") || fm1_front_same(key, "KNOB BANK")) return "SELECT";
    if (fm1_front_same(key, "BT")) return "HOME";
    if (fm1_front_same(key, "GLOBE")) return "GLO";
    if (fm1_front_same(key, "PATCH")) return "SAVE";
    if (fm1_front_same(key, "FADER BANK")) return "EDIT";
    if (fm1_front_same(key, "CHORD")) return "GLO+PLAY";
    if (fm1_front_same(key, "<")) return "SEL<";
    if (fm1_front_same(key, ">")) return "SEL>";
    if (fm1_front_same(key, "REC+<")) return "REC+SEL<";
    if (fm1_front_same(key, "REC+>")) return "REC+SEL>";
    if (fm1_front_same(key, "KEY")) return "KEY";
    if (fm1_front_same(key, "RPT")) return "GLO+REC";
    if (fm1_front_same(key, "KEYS")) return "GLO+SCL";
    if (key[0] == 'P' && key[1] == 'A' && key[2] == 'D') /* PAD, PAD 1-16: the white keys, where they are pads */
        return fm1_front_white_pads(panel, &fm1_front_state) ? "WHITE" : NULL;
    if (fm1_front_same(key, "PARA")) {
        if (fm1_front_same(action, "FILT ENV") || fm1_front_same(action, "VCF")) return "ENV";
        return panel->page == PAGE_MANUAL ? "ALGO" : "GLO+LFO";
    }
    return NULL;
}

#endif /* FM1_FRONT_INC */
