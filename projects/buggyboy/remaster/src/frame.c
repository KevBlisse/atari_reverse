/* frame.c — the whole-frame render composition (draw_frame @0x12e22 + draw_game_objects @0x12dcc).
 *
 * The platform-independent per-frame render pipeline hoisted out of the on-target shell so the shell
 * (render/atari/game_main.c) calls ONE composition; the bench (bench_main.c) deliberately mirrors it with staged HUD scalars,
 * and so `make test` can run the shell's real frame end-to-end (the composed-frame differential in
 * test/equiv.py) and catch any missing wiring between the individually-verified stages. Transcribed
 * one-for-one from the shell's former draw_frame body; the order — including the wrap-frame's second
 * geometry build (the caller's game-update step builds once before the event tail, this builds again
 * off the ring bands the tail poked) — is preserved exactly (see STATUS "Perf": do not dedupe it).
 */
#include "game.h"

#if defined(_WIN32) && defined(BB_TRACE_GANTRY)
#include <stdio.h>
#include <string.h>
/* Default: full screen. Set these defines to focus on the gantry rectangle. */
#ifndef BB_GANTRY_X0
#define BB_GANTRY_X0 0
#endif
#ifndef BB_GANTRY_X1
#define BB_GANTRY_X1 319
#endif
#ifndef BB_GANTRY_Y0
#define BB_GANTRY_Y0 0
#endif
#ifndef BB_GANTRY_Y1
#define BB_GANTRY_Y1 199
#endif
static uint8_t bb_before[SCREEN_BYTES];
static unsigned bb_frame;
static void bb_snapshot(const Framebuffer *fb) { memcpy(bb_before, fb->px, SCREEN_BYTES); }
static void bb_diff(const Framebuffer *fb, const char *stage, unsigned p24) {
    unsigned changed = 0, first = SCREEN_BYTES, last = 0;
    /* Observe every drawing stage: do not assume the gantry uses fixed_objects. */
    if (bb_frame < 1300 || bb_frame > 1600)
        return;
    for (unsigned y = BB_GANTRY_Y0; y <= BB_GANTRY_Y1 && y < SCREEN_H; ++y)
        for (unsigned g = BB_GANTRY_X0 / 16; g <= BB_GANTRY_X1 / 16 && g < SCREEN_W / 16; ++g)
            for (unsigned b = 0; b < 8; ++b) {
                unsigned off = y * SCREEN_ROW_BYTES + g * 8 + b;
                if (bb_before[off] == fb->px[off]) continue;
                if (changed < 3 && (bb_frame % 5 == 0 || bb_frame == 1384))
                    fprintf(stderr, "  off=%u y=%u xgroup=%u %02X->%02X\n",
                            off, y, g, bb_before[off], fb->px[off]);
                if (off < first) first = off;
                if (off > last) last = off;
                ++changed;
            }
    if (!changed || (bb_frame % 5 != 0 && bb_frame != 1384)) return;
    fprintf(stderr, "[GANTRY] frame=%u stage=%s p24=%02X changed=%u first=%u last=%u\n",
            bb_frame, stage, p24, changed, changed ? first : 0, changed ? last : 0);
}
#define BB_BEFORE() do { if (bb_frame >= 1300 && bb_frame <= 1600) bb_snapshot(fb); } while (0)
#define BB_FIXED_BEFORE() BB_BEFORE()
#define BB_AFTER(name) bb_diff(fb, name, objlist->p24_flag)
#else
#define BB_BEFORE() ((void)0)
#define BB_FIXED_BEFORE() ((void)0)
#define BB_AFTER(name) ((void)0)
#endif


/* Windows-only, opt-in fixed-object gantry capture. The output is a tagged
 * native-state bundle, NOT an Atari flat image: pointer addresses are never
 * serialized as though they were 68000 addresses. The converter must validate
 * and relocate every section before feeding the oracle. */
#if defined(_WIN32)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
static void bb_capture_chunk(FILE *f, const char tag[4], const void *p, uint32_t n) {
    unsigned char header[8] = {
        (unsigned char)tag[0], (unsigned char)tag[1],
        (unsigned char)tag[2], (unsigned char)tag[3],
        (unsigned char)n, (unsigned char)(n >> 8),
        (unsigned char)(n >> 16), (unsigned char)(n >> 24)
    };
    fwrite(header, 1, sizeof header, f);
    fwrite(p, 1, n, f);
}
static void bb_capture_state(const RmScene *sc, const Framebuffer *fb,
                             unsigned frame, const char *suffix) {
    const char *dir = getenv("BB_GANTRY_CAPTURE_DIR");
    if (!dir || !*dir) return;
    if (frame < 1300 || frame > 1600 || frame % 10 != 0) return;
    char path[1024];
    int n = snprintf(path, sizeof path, "%s/gantry_%04u_%s.bbcap", dir, frame, suffix);
    if (n <= 0 || (size_t)n >= sizeof path) return;
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "GANTRY CAPTURE: cannot open %s\\n", path); return; }
    const unsigned char magic[8] = {'B','B','C','A','P','0','1',0};
    fwrite(magic, 1, sizeof magic, f);
    uint32_t scalars[6] = {frame, sc->objlist->view_flags,
        sc->objlist->view_parity, sc->objlist->bonus_timer,
        (uint16_t)sc->objlist->obj_scan_off, sc->objlist->p24_flag};
    bb_capture_chunk(f, "META", scalars, sizeof scalars);
    bb_capture_chunk(f, "FB00", fb->px, SCREEN_BYTES);
    /* Arena sizes are defined by assets.h; this includes every record and
     * graphics offset reachable by the fixed-object dispatcher. */
    bb_capture_chunk(f, "BUFA", sc->objlist->buf_a,
                     RM_GFX_OFF - RM_TABLES_OFF);
    bb_capture_chunk(f, "BUFC", sc->objlist->buf_c,
                     RM_AUX_OFF - RM_GFX_OFF);
    bb_capture_chunk(f, "LIST", sc->obj_fixed_list, 1024);
    /* Earlier sprite passes read this separate display stream. */
    bb_capture_chunk(f, "SPRL", sc->obj_sprite_disp, 1024);
    bb_capture_chunk(f, "FLAG", sc->ring_st, RM_RING_ROWS * RM_RING_ROW_BYTES);
    bb_capture_chunk(f, "XOFF", sc->objlist->xoff_tbl,
                     RM_CTRL_ALLOC_BYTES - RM_CTRL_WIDTH_OFF - 2);
    bb_capture_chunk(f, "COLR", sc->objlist->color_pairs, 128);
    bb_capture_chunk(f, "VIEW", sc->objlist->view_xform, 256);
    bb_capture_chunk(f, "PFXT", sc->objlist->objsh2p_tbl, 256);
    bb_capture_chunk(f, "JUMP", sc->objlist->jumptable, 256);
    if (fclose(f) != 0) fprintf(stderr, "GANTRY CAPTURE: write failed %s\\n", path);
    else fprintf(stderr, "GANTRY CAPTURE: %s\\n", path);
}
#else
#define bb_capture_state(sc, fb, frame, suffix) ((void)0)
#endif

/* UNIFIED ST/STE binary (PERF30 C5 slice 1): route the road fine-scroll through rm_blit_road_scroll_fn —
 * a function pointer bound ONCE at boot (rm_blit_bind_all) to the hardware-blitter dispatch when a
 * blitter is present, else left at the C reference. The seam lives HERE, at the sole call site, and not
 * in include/game.h — exactly as object_list.c does for the two object engines, and in the same shape: an
 * UPPERCASE seam name, so the call site reads as a seam and the real function's name is never shadowed.
 * Gated on RM_BLITTER (the m68k target build), so the host differential keeps calling the C reference. */
#ifdef RM_BLITTER
#include "scroll_const.h"
#define RM_BLIT_ROAD_SCROLL rm_blit_road_scroll_fn        /* boot-bound: blitter (STE) or the C reference */
#else
#define RM_BLIT_ROAD_SCROLL rm_blit_road_scroll
#endif

void rm_draw_frame(const RmScene *sc, Framebuffer *fb) {
#if defined(_WIN32)
    static unsigned bb_capture_frame;
    ++bb_capture_frame;
#endif
#if defined(_WIN32) && defined(BB_TRACE_GANTRY)
    ++bb_frame;
#endif
    GobjPrefixState *pfx = sc->pfx;
    ObjListCtx *objlist = sc->objlist;

    rm_gobj_prefix(pfx, sc->pfx_assets);             /* off-frame: advance view_parity/anim/marker */
    /* The three LIVE globals the object dispatcher reads. In the original each is one word/byte it
     * loads at draw time, so all three must be refreshed HERE, every frame, not captured at a leg
     * start: view_parity and bonus_timer are advanced by the prefix just above (bonus_timer opens the
     * 5-flag window that clamps low object types), and p24_flag is score_str[1], which §I bumps at
     * every checkpoint. */
    objlist->view_parity = pfx->view_parity;         /* the dispatcher (handler_lo) reads the advanced parity */
    objlist->bonus_timer = pfx->bonus_timer;
    objlist->p24_flag = sc->hud_text[RM_HUD_P24_DIGIT_OFF];
    objlist->px = fb->px;                            /* the dispatcher's draw target: this frame's buffer */
    rm_build_road_geometry(sc->pose, sc->src, sc->ring, sc->ctrl, sc->scanline);
    sc->road->width_tbl = sc->ctrl + RM_CTRL_WIDTH_OFF;
    /* The object dispatcher's per-row x-offset table aliases the freshly-built road control table in
     * the original (obj_xoff_tbl == road_width_tbl + 2), so rebind it to this frame's ctrl. */
    objlist->xoff_tbl = sc->ctrl + RM_CTRL_WIDTH_OFF + 2;
    sc->scroll->seg_head = sc->pose->seg_head;       /* the scroll step follows the near slope */
    BB_BEFORE(); rm_render_road(sc->road, fb); BB_AFTER("road");
    BB_BEFORE(); RM_BLIT_ROAD_SCROLL(sc->scroll, sc->shifted, fb); BB_AFTER("road_scroll");

    /* --- draw_game_objects tree ---
     * GAME_DUMP_STAGE (on-target debug builds only; never defined for the host lib) cuts the frame
     * short after a chosen stage, so a headless run's SCREEN.BIN dump is a partial frame and can
     * pinpoint the first stage whose on-target output diverges from the host reference. */
#if defined(GAME_DUMP_STAGE) && GAME_DUMP_STAGE == 0
    return;
#endif
    BB_BEFORE(); rm_draw_ground(sc->ground, sc->ground_assets, fb); BB_AFTER("ground");
#if defined(GAME_DUMP_STAGE) && GAME_DUMP_STAGE == 1
    return;
#endif
    BB_BEFORE(); rm_draw_fg_sprite(sc->sprite, sc->sprite_assets, fb); BB_AFTER("foreground");
#if defined(GAME_DUMP_STAGE) && GAME_DUMP_STAGE == 2
    return;
#endif

    /* The dispatcher's flag streams and the pass split all come from the LIVE ring: the sprite passes
     * walk the serialized grid from row 1, the fixed pass from row 12 — the same aliasing the original
     * gets from pointing into its flat row grid. */
    int count = rm_ring_sprite_count(sc->ring);
    /* Capture the input to pass 1 before any variable-object drawing. */
    bb_capture_state(sc, fb, bb_capture_frame, "pre_objects");
    BB_BEFORE();
    if (count - 1 >= 0)                               /* pass 1: the active sprite rows */
        rm_draw_object_list(objlist, sc->obj_sprite_disp, 0,
                            sc->ring_st + GOBJ_SPRITE_PASS_ROW * RM_RING_ROW_BYTES, 0,
                            (uint16_t)(count - 1), GOBJ_D6_INIT, (uint16_t)count);
    BB_AFTER("object_pass_1");
#if defined(GAME_DUMP_STAGE) && GAME_DUMP_STAGE == 3
    return;
#endif
    BB_BEFORE(); rm_draw_object(sc->object, fb); BB_AFTER("scaled_object");
    BB_BEFORE();
    if (GOBJ_SPRITE_LAST - count >= 0)               /* pass 2: the remaining rows */
        rm_draw_object_list(objlist, sc->obj_sprite_disp, (uint16_t)(count * GOBJ_ROW_A5_STRIDE),
                            sc->ring_st + GOBJ_SPRITE_PASS_ROW * RM_RING_ROW_BYTES,
                            (uint16_t)(count * GOBJ_ROW_A3_STRIDE),
                            (uint16_t)(GOBJ_SPRITE_LAST - count),
                            (uint16_t)(GOBJ_D6_INIT - count * GOBJ_D6_ROW_STEP), 0);
    BB_AFTER("object_pass_2");
    if ((objlist->view_flags & GOBJ_VIEW_REAR) == 0) {   /* pass 3 (fixed) + buggy, view-ordered */
        bb_capture_state(sc, fb, bb_capture_frame, "pre_fixed");
        BB_FIXED_BEFORE();
        rm_draw_object_list(objlist, sc->obj_fixed_list, 0,
                            sc->ring_st + GOBJ_FIXED_PASS_ROW * RM_RING_ROW_BYTES, 0, 0, 0, 0);
        BB_AFTER("fixed_objects");
        BB_BEFORE(); rm_draw_buggy(sc->sprite, sc->sprite_assets, fb); BB_AFTER("buggy");
    } else {
        BB_BEFORE(); rm_draw_buggy(sc->sprite, sc->sprite_assets, fb); BB_AFTER("buggy");
        bb_capture_state(sc, fb, bb_capture_frame, "pre_fixed");
        BB_FIXED_BEFORE();
        rm_draw_object_list(objlist, sc->obj_fixed_list, 0,
                            sc->ring_st + GOBJ_FIXED_PASS_ROW * RM_RING_ROW_BYTES, 0, 0, 0, 0);
        BB_AFTER("fixed_objects");
    }

    /* Fan the flag-sequence state pfx OWNS into the HUD view (phase-4 bars + phase-5 colour cursor),
     * right before the HUD draw — the original's g_draw_game_objects-then-g_draw_hud order, and after
     * rm_gobj_prefix above (which advances these). Without it the flag bars never appear. */
    rm_gobj_hud_view(pfx, sc->hud);
    BB_BEFORE(); rm_draw_hud(sc->hud, sc->hud_assets, fb); BB_AFTER("hud");
}
