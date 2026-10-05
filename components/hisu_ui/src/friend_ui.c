#include "friend_ui.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#include "lvgl.h"


static const char *TAG = "friend_ui";


// ============================================================
// Face configuration
// ============================================================

#define EYE_WIDTH 48
#define EYE_HEIGHT_OPEN 68
#define EYE_HEIGHT_CLOSED 8
#define EYE_SPACING 96
#define EYE_CENTER_Y 104
#define EYE_RADIUS 24

#define HAPPY_EYE_D 56
#define HAPPY_EYE_ARC_W 10

#define MOUTH_D 64
#define MOUTH_BOTTOM 180
#define MOUTH_ARC_W 8

#define CHEEK_W 30
#define CHEEK_H 16
#define CHEEK_CENTER_Y 132

// Mi mat (ve bang duong thang day, khong ton RAM nhu xoay object)
#define LID_LEN 92
#define LID_T 60

#define BLINK_MIN_DELAY_MS 2000
#define BLINK_MAX_DELAY_MS 4500

#define GAZE_MIN_DELAY_MS 2500
#define GAZE_MAX_DELAY_MS 5500
#define GAZE_OFFSET_PX 10

#define IDLE_MIN_DELAY_MS 6000
#define IDLE_MAX_DELAY_MS 12000

// Phat sang quanh mat. Neu bi giat/lag tren CYD thi dat = 0
#define ENABLE_GLOW 1

#define EYE_COLOR 0x45E6FF
#define HIGHLIGHT_COLOR 0xEFFFFF
#define CHEEK_COLOR 0xFF7EA8
#define BACKGROUND_COLOR 0x05080A


// ============================================================
// Emotion presets
// ============================================================

typedef struct
{
    int eye_height;
    bool arc_eyes;        // true: mat cung "^ ^"
    int mouth_v;          // >0 cuoi, <0 mếu/gian; do lon = do cong
    int mouth_rot;        // nghieng mieng (do) -> nhech mieng
    bool mouth_o;         // mieng tron "o"
    int cheek_opa;
    bool zzz;
    int blink_ms;         // 0 = khong chop mat
    int lid[4];           // {trai cover, trai goc, phai cover, phai goc}; goc > 0 = phia trong thap xuong
    int entry_action;     // hanh dong tu phat khi doi sang cam xuc nay (-1 = khong)
} emotion_cfg_t;

static const emotion_cfg_t CFG_NEUTRAL =
    { EYE_HEIGHT_OPEN, false, 30, 0, false, 0, false, 110, { 0, 0, 0, 0 }, -1 };

static const emotion_cfg_t CFG_HAPPY =
    { 56, true, 68, 0, false, 120, false, 0, { 0, 0, 0, 0 }, FRIEND_ACTION_BOUNCE };

static const emotion_cfg_t CFG_SLEEPY =
    { 60, false, 16, 0, false, 0, true, 380, { 26, 0, 26, 0 }, -1 };

static const emotion_cfg_t CFG_ANGRY =
    { 60, false, -24, 0, false, 0, false, 110, { 22, 24, 22, 24 }, FRIEND_ACTION_SHAKE };

static const emotion_cfg_t CFG_SURPRISED =
    { 82, false, 30, 0, true, 0, false, 0, { 0, 0, 0, 0 }, FRIEND_ACTION_STARTLE };

static const emotion_cfg_t CFG_MISCHIEVOUS =
    { 66, false, 42, 14, false, 50, false, 110, { 0, 0, 28, 14 }, FRIEND_ACTION_SNEAKY };


// ============================================================
// Action keyframe tracks (phong cach hoat hinh Cozmo/Emo)
// t: 0..1000 (phan tram thoi gian * 10), v: gia tri tai thoi diem do
// ============================================================

typedef struct
{
    int16_t t;
    int16_t v;
} kf_t;

typedef enum
{
    TR_DX,       // dich ca khuon mat theo x (px)
    TR_DY,       // dich ca khuon mat theo y (px)
    TR_GAZE,     // liec mat theo x (px)
    TR_SQUASH,   // <0: bet (mat lun, rong ra), >0: keo dai (%)
    TR_WINK,     // do cao mat phai (%), 100 = mo
    TR_BLINK,    // do cao 2 mat (%), 100 = mo
    TR_COUNT
} track_id_t;

typedef struct
{
    const kf_t *kf;
    uint8_t n;
} track_t;

typedef struct
{
    uint16_t duration_ms;
    track_t tr[TR_COUNT];
} action_def_t;

#define KF(a) a, (uint8_t)(sizeof(a) / sizeof((a)[0]))

// SHAKE: lac dau "khong"
static const kf_t kf_shake_dx[] = { {0,0}, {100,-10}, {250,10}, {400,-8}, {550,7}, {700,-4}, {850,2}, {1000,0} };

// NOD: gat dau "co"
static const kf_t kf_nod_dy[] = { {0,0}, {180,9}, {380,-3}, {580,8}, {800,-2}, {1000,0} };

// BOUNCE: ngoi xuong lay da -> bat len -> tiep dat -> nay them lan nua
static const kf_t kf_bounce_dy[] = { {0,0}, {130,5}, {330,-22}, {520,0}, {600,3}, {780,-10}, {920,0}, {1000,0} };
static const kf_t kf_bounce_sq[] = { {0,0}, {130,-18}, {330,14}, {520,-20}, {600,-8}, {780,8}, {920,-8}, {1000,0} };

// WINK: nham mat phai, nghieng dau nhe
static const kf_t kf_wink_w[] = { {0,100}, {220,0}, {620,0}, {900,100}, {1000,100} };
static const kf_t kf_wink_dx[] = { {0,0}, {220,-4}, {620,-4}, {1000,0} };

// LOOK_AROUND: liec trai -> phai -> ve giua, chop mat luc doi huong
static const kf_t kf_look_g[] = { {0,0}, {100,-16}, {330,-16}, {480,16}, {750,16}, {900,0}, {1000,0} };
static const kf_t kf_look_b[] = { {0,100}, {390,100}, {430,10}, {480,100}, {1000,100} };

// STARTLE: giat minh nhay len, mat keo dai, roi ha xuong
static const kf_t kf_startle_dy[] = { {0,0}, {80,-18}, {550,-16}, {1000,0} };
static const kf_t kf_startle_sq[] = { {0,0}, {80,28}, {550,22}, {700,-14}, {1000,0} };

// GIGGLE: rung nguoi, mat nheo lai
static const kf_t kf_giggle_dx[] = { {0,0}, {60,4}, {120,-4}, {180,4}, {240,-4}, {300,4}, {360,-4}, {420,4},
                                     {480,-4}, {540,4}, {600,-4}, {660,3}, {720,-3}, {780,2}, {840,-2}, {1000,0} };
static const kf_t kf_giggle_dy[] = { {0,0}, {80,-5}, {160,0}, {240,-5}, {320,0}, {400,-5}, {480,0}, {560,-4},
                                     {640,0}, {720,-3}, {800,0}, {1000,0} };
static const kf_t kf_giggle_sq[] = { {0,0}, {100,-25}, {800,-25}, {1000,0} };

// SNEAKY: liec len phai, nhin lom lom, chop mat roi quay lai
static const kf_t kf_sneaky_g[] = { {0,0}, {110,18}, {700,18}, {820,0}, {1000,0} };
static const kf_t kf_sneaky_b[] = { {0,100}, {690,100}, {750,15}, {810,100}, {1000,100} };

static const action_def_t s_actions[FRIEND_ACTION_COUNT] = {
    [FRIEND_ACTION_SHAKE]       = { 700,  { [TR_DX] = { KF(kf_shake_dx) } } },
    [FRIEND_ACTION_NOD]         = { 700,  { [TR_DY] = { KF(kf_nod_dy) } } },
    [FRIEND_ACTION_BOUNCE]      = { 1000, { [TR_DY] = { KF(kf_bounce_dy) }, [TR_SQUASH] = { KF(kf_bounce_sq) } } },
    [FRIEND_ACTION_WINK]        = { 900,  { [TR_WINK] = { KF(kf_wink_w) }, [TR_DX] = { KF(kf_wink_dx) } } },
    [FRIEND_ACTION_LOOK_AROUND] = { 2200, { [TR_GAZE] = { KF(kf_look_g) }, [TR_BLINK] = { KF(kf_look_b) } } },
    [FRIEND_ACTION_STARTLE]     = { 800,  { [TR_DY] = { KF(kf_startle_dy) }, [TR_SQUASH] = { KF(kf_startle_sq) } } },
    [FRIEND_ACTION_GIGGLE]      = { 1200, { [TR_DX] = { KF(kf_giggle_dx) }, [TR_DY] = { KF(kf_giggle_dy) },
                                            [TR_SQUASH] = { KF(kf_giggle_sq) } } },
    [FRIEND_ACTION_SNEAKY]      = { 1600, { [TR_GAZE] = { KF(kf_sneaky_g) }, [TR_BLINK] = { KF(kf_sneaky_b) } } },
};

// Tro dien khi ranh, tuy theo cam xuc hien tai
static const friend_action_t IDLE_NEUTRAL[] = { FRIEND_ACTION_LOOK_AROUND, FRIEND_ACTION_SNEAKY, FRIEND_ACTION_WINK, FRIEND_ACTION_NOD };
static const friend_action_t IDLE_HAPPY[] = { FRIEND_ACTION_BOUNCE, FRIEND_ACTION_GIGGLE, FRIEND_ACTION_WINK };
static const friend_action_t IDLE_MISCHIEVOUS[] = { FRIEND_ACTION_SNEAKY, FRIEND_ACTION_GIGGLE, FRIEND_ACTION_LOOK_AROUND };
static const friend_action_t IDLE_ANGRY[] = { FRIEND_ACTION_SHAKE };


// ============================================================
// Face objects & state
// ============================================================

static lv_obj_t *s_left_eye = NULL;
static lv_obj_t *s_right_eye = NULL;
static lv_obj_t *s_hl[4] = { NULL };

static lv_obj_t *s_left_arc_eye = NULL;
static lv_obj_t *s_right_arc_eye = NULL;

static lv_obj_t *s_lid[2] = { NULL };
static lv_point_precise_t s_lid_pts[2][2];
static int s_lid_last[2][6];
static bool s_lid_valid[2] = { false, false };

static lv_obj_t *s_mouth = NULL;
static lv_obj_t *s_mouth_o = NULL;
static lv_obj_t *s_left_cheek = NULL;
static lv_obj_t *s_right_cheek = NULL;
static lv_obj_t *s_zzz = NULL;

static lv_timer_t *s_blink_timer = NULL;
static lv_timer_t *s_gaze_timer = NULL;
static lv_timer_t *s_idle_timer = NULL;

static int s_cx = 120;
static int s_left_cx = 72;
static int s_right_cx = 168;

// Trang thai "goc" (do emotion dieu khien)
static int s_eye_h = EYE_HEIGHT_CLOSED;   // bat dau bang mat nham -> "thuc day"
static int s_target_eye_h = EYE_HEIGHT_OPEN;
static int s_mouth_v = 30;
static int s_cheek_opa = 0;
static int s_zzz_rise = 0;
static int s_lid_from[4], s_lid_to[4], s_lid_cur[4];
static int s_blink_ms = 110;
static bool s_eyes_closed = false;

// Trang thai "phu" (do action / gaze dieu khien)
static int s_face_dx = 0;
static int s_face_dy = 0;
static int s_gaze_x = 0;
static int s_squash = 0;
static int s_wink_pct = 100;
static int s_blink_pct = 100;

static bool s_action_running = false;
static bool s_idle_enabled = true;

static friend_emotion_t s_current_emotion = FRIEND_EMOTION_NEUTRAL;

// Cache opacity de khong invalidate lai vo ich
static int s_hl_opa_cache[4] = { -1, -1, -1, -1 };
static int s_arc_opa_cache[2] = { -1, -1 };

// Dia chi cac bien nay duoc dung lam "key" quan ly animation
static int s_anim_eye_key, s_anim_gaze_key, s_anim_mouth_key, s_anim_cheek_key;
static int s_anim_zzz_key, s_anim_lid_key, s_anim_action_key;

static const action_def_t *s_action_def = NULL;


// ============================================================
// Helpers
// ============================================================

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static uint32_t random_range(uint32_t min_ms, uint32_t max_ms)
{
    return min_ms + (esp_random() % (max_ms - min_ms + 1));
}

static int sin_deg(int deg)
{
    deg %= 360;

    if (deg < 0)
    {
        deg += 360;
    }

    return lv_trigo_sin(deg);
}

static int cos_deg(int deg)
{
    return sin_deg(deg + 90);
}

static lv_obj_t *create_rect(lv_obj_t *parent, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    return obj;
}

static lv_obj_t *create_line_arc(lv_obj_t *parent, int diameter, int width, uint32_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);

    lv_obj_set_size(arc, diameter, diameter);
    lv_arc_set_rotation(arc, 0);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);

    return arc;
}

static void start_anim(void *key, lv_anim_exec_xcb_t cb, int32_t from, int32_t to,
                       uint32_t ms, lv_anim_path_cb_t path)
{
    lv_anim_delete(key, cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, key);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void set_opa_cached(lv_obj_t *obj, int *cache, int opa)
{
    opa = clamp_int(opa, 0, 255);

    if (*cache != opa)
    {
        *cache = opa;
        lv_obj_set_style_opa(obj, opa, LV_PART_MAIN);
    }
}

static int highlight_opa(int eye_h)
{
    // Highlight mo dan khi mat nho lai -> khong bi cat xau luc chop mat
    return ((eye_h - 34) * 255) / 20;
}

// Noi suy keyframe voi smoothstep
static int track_eval(const track_t *tr, int t, int def)
{
    if (tr->kf == NULL || tr->n == 0)
    {
        return def;
    }

    const kf_t *k = tr->kf;

    if (t <= k[0].t)
    {
        return k[0].v;
    }

    for (int i = 1; i < tr->n; i++)
    {
        if (t <= k[i].t)
        {
            int dt = k[i].t - k[i - 1].t;

            if (dt <= 0)
            {
                return k[i].v;
            }

            int p = ((t - k[i - 1].t) * 1000) / dt;
            int s = ((p * p) / 1000) * (3000 - 2 * p) / 1000;

            return k[i - 1].v + ((k[i].v - k[i - 1].v) * s) / 1000;
        }
    }

    return k[tr->n - 1].v;
}


// ============================================================
// Layout: tinh lai vi tri moi bo phan (goi moi khung animation)
// ============================================================

static void update_lid(int idx, int eye_cx, int eye_top, int eye_h, int cover, int angle, bool is_left)
{
    lv_obj_t *lid = s_lid[idx];

    cover = min_int(cover, eye_h - 6);   // luon chua lai 1 duong mat mong khi chop

    if (cover <= 0)
    {
        if (s_lid_valid[idx])
        {
            lv_obj_add_flag(lid, LV_OBJ_FLAG_HIDDEN);
            s_lid_valid[idx] = false;
        }

        return;
    }

    int half = LID_LEN / 2;
    int dxl = half * cos_deg(angle) / 32767;
    int dyl = half * sin_deg(angle) / 32767;
    int sgn = is_left ? 1 : -1;
    int cy0 = eye_top + cover - (LID_T / 2);

    int x1 = eye_cx - dxl;
    int y1 = cy0 - (sgn * dyl);
    int x2 = eye_cx + dxl;
    int y2 = cy0 + (sgn * dyl);

    int minx = min_int(x1, x2);
    int miny = min_int(y1, y2);

    int key[6] = { minx, miny, x1 - minx, y1 - miny, x2 - minx, y2 - miny };

    if (s_lid_valid[idx] && memcmp(key, s_lid_last[idx], sizeof(key)) == 0)
    {
        return;
    }

    memcpy(s_lid_last[idx], key, sizeof(key));

    s_lid_pts[idx][0].x = key[2];
    s_lid_pts[idx][0].y = key[3];
    s_lid_pts[idx][1].x = key[4];
    s_lid_pts[idx][1].y = key[5];

    lv_obj_remove_flag(lid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(lid, minx, miny);
    lv_line_set_points(lid, s_lid_pts[idx], 2);

    s_lid_valid[idx] = true;
}

static void place_zzz(void)
{
    lv_obj_set_pos(s_zzz, s_cx + 52 + s_face_dx, EYE_CENTER_Y - 56 - s_zzz_rise + s_face_dy);
}

static void layout_all(void)
{
    if (s_left_eye == NULL || s_mouth == NULL)
    {
        return;
    }

    // ---- Kich thuoc mat: goc * chop mat * squash/stretch * nhay mat ----
    int hb = (s_eye_h * s_blink_pct) / 100;
    int h_left = (hb * (100 + s_squash)) / 100;
    int h_right = (h_left * s_wink_pct) / 100;

    h_left = clamp_int(h_left, EYE_HEIGHT_CLOSED, 140);
    h_right = clamp_int(h_right, EYE_HEIGHT_CLOSED, 140);

    int w = (EYE_WIDTH * (100 - (s_squash / 2))) / 100;

    int cy = EYE_CENTER_Y + s_face_dy;
    int ex = s_face_dx + s_gaze_x;

    lv_obj_set_size(s_left_eye, w, h_left);
    lv_obj_set_pos(s_left_eye, s_left_cx + ex - (w / 2), cy - (h_left / 2));

    lv_obj_set_size(s_right_eye, w, h_right);
    lv_obj_set_pos(s_right_eye, s_right_cx + ex - (w / 2), cy - (h_right / 2));

    set_opa_cached(s_hl[0], &s_hl_opa_cache[0], highlight_opa(h_left));
    set_opa_cached(s_hl[1], &s_hl_opa_cache[1], highlight_opa(h_left));
    set_opa_cached(s_hl[2], &s_hl_opa_cache[2], highlight_opa(h_right));
    set_opa_cached(s_hl[3], &s_hl_opa_cache[3], highlight_opa(h_right));

    // ---- Mat cung "^ ^" (khi HAPPY) ----
    lv_obj_set_pos(s_left_arc_eye, s_left_cx + s_face_dx - (HAPPY_EYE_D / 2), cy - 8);
    lv_obj_set_pos(s_right_arc_eye, s_right_cx + s_face_dx - (HAPPY_EYE_D / 2), cy - 8);

    set_opa_cached(s_left_arc_eye, &s_arc_opa_cache[0], (s_blink_pct * 255) / 100);
    set_opa_cached(s_right_arc_eye, &s_arc_opa_cache[1], (min_int(s_blink_pct, s_wink_pct) * 255) / 100);

    // ---- Mi mat ----
    update_lid(0, s_left_cx + ex, cy - (h_left / 2), h_left, s_lid_cur[0], s_lid_cur[1], true);
    update_lid(1, s_right_cx + ex, cy - (h_right / 2), h_right, s_lid_cur[2], s_lid_cur[3], false);

    // ---- Mieng ----
    int mx = s_cx + s_face_dx - (MOUTH_D / 2);
    int my = (s_mouth_v >= 0) ? (MOUTH_BOTTOM - MOUTH_D) : (MOUTH_BOTTOM - 4);

    lv_obj_set_pos(s_mouth, mx, my + s_face_dy);
    lv_obj_set_pos(s_mouth_o, s_cx + s_face_dx - 9, MOUTH_BOTTOM - 21 + s_face_dy);

    // ---- Ma hong + Zzz ----
    lv_obj_set_pos(s_left_cheek, s_left_cx + s_face_dx - 10 - (CHEEK_W / 2), CHEEK_CENTER_Y - (CHEEK_H / 2) + s_face_dy);
    lv_obj_set_pos(s_right_cheek, s_right_cx + s_face_dx + 10 - (CHEEK_W / 2), CHEEK_CENTER_Y - (CHEEK_H / 2) + s_face_dy);

    place_zzz();
}


// ============================================================
// Animation callbacks
// ============================================================

static void anim_eye_cb(void *key, int32_t v)
{
    (void)key;
    s_eye_h = v;
    layout_all();
}

static void anim_gaze_cb(void *key, int32_t v)
{
    (void)key;
    s_gaze_x = v;
    layout_all();
}

static void anim_mouth_cb(void *key, int32_t v)
{
    (void)key;

    s_mouth_v = v;

    int a = clamp_int(v < 0 ? -v : v, 4, 80);

    if (v >= 0)
    {
        lv_arc_set_bg_angles(s_mouth, 90 - a, 90 + a);     // cung duoi = cuoi
    }
    else
    {
        lv_arc_set_bg_angles(s_mouth, 270 - a, 270 + a);   // cung tren = mieu/gian
    }

    layout_all();
}

static void anim_cheek_cb(void *key, int32_t v)
{
    (void)key;

    s_cheek_opa = clamp_int(v, 0, 255);
    lv_obj_set_style_opa(s_left_cheek, s_cheek_opa, LV_PART_MAIN);
    lv_obj_set_style_opa(s_right_cheek, s_cheek_opa, LV_PART_MAIN);
}

static void anim_lid_cb(void *key, int32_t v)
{
    (void)key;

    for (int i = 0; i < 4; i++)
    {
        s_lid_cur[i] = s_lid_from[i] + (((s_lid_to[i] - s_lid_from[i]) * v) / 1000);
    }

    layout_all();
}

// v: 0..1000 lap vo han -> chu z bay len va mo dan
static void anim_zzz_cb(void *key, int32_t v)
{
    (void)key;

    int opa = (v < 600) ? (v * 255 / 600) : ((1000 - v) * 255 / 400);

    s_zzz_rise = v * 18 / 1000;
    place_zzz();
    lv_obj_set_style_opa(s_zzz, clamp_int(opa, 0, 255), LV_PART_MAIN);
}

static void anim_action_cb(void *key, int32_t v)
{
    (void)key;

    const action_def_t *d = s_action_def;

    if (d == NULL)
    {
        return;
    }

    s_face_dx = track_eval(&d->tr[TR_DX], v, 0);
    s_face_dy = track_eval(&d->tr[TR_DY], v, 0);
    s_gaze_x = track_eval(&d->tr[TR_GAZE], v, 0);
    s_squash = track_eval(&d->tr[TR_SQUASH], v, 0);
    s_wink_pct = clamp_int(track_eval(&d->tr[TR_WINK], v, 100), 0, 100);
    s_blink_pct = clamp_int(track_eval(&d->tr[TR_BLINK], v, 100), 0, 100);

    layout_all();

    if (v >= 1000)
    {
        s_action_running = false;
    }
}


// ============================================================
// Timers: blink, idle gaze, idle actions
// ============================================================

static void blink_timer_cb(lv_timer_t *timer)
{
    if (s_eyes_closed)
    {
        start_anim(&s_anim_eye_key, anim_eye_cb, s_eye_h, s_target_eye_h,
                   ((s_blink_ms * 3) / 4) + 10, lv_anim_path_ease_out);

        s_eyes_closed = false;
        lv_timer_set_period(timer, random_range(BLINK_MIN_DELAY_MS, BLINK_MAX_DELAY_MS));
    }
    else if (s_blink_ms > 0)
    {
        start_anim(&s_anim_eye_key, anim_eye_cb, s_eye_h, EYE_HEIGHT_CLOSED,
                   (s_blink_ms * 2) / 3, lv_anim_path_ease_in);

        s_eyes_closed = true;
        lv_timer_set_period(timer, s_blink_ms);
    }
    else
    {
        lv_timer_set_period(timer, random_range(BLINK_MIN_DELAY_MS, BLINK_MAX_DELAY_MS));
    }

    lv_timer_reset(timer);
}

static void gaze_timer_cb(lv_timer_t *timer)
{
    if (!s_action_running)
    {
        int target = 0;

        if (s_current_emotion == FRIEND_EMOTION_NEUTRAL)
        {
            static const int choices[4] = { -GAZE_OFFSET_PX, 0, GAZE_OFFSET_PX, 0 };
            target = choices[esp_random() % 4];
        }

        start_anim(&s_anim_gaze_key, anim_gaze_cb, s_gaze_x, target, 260, lv_anim_path_ease_out);
    }

    lv_timer_set_period(timer, random_range(GAZE_MIN_DELAY_MS, GAZE_MAX_DELAY_MS));
    lv_timer_reset(timer);
}

static void idle_timer_cb(lv_timer_t *timer)
{
    lv_timer_set_period(timer, random_range(IDLE_MIN_DELAY_MS, IDLE_MAX_DELAY_MS));
    lv_timer_reset(timer);

    if (!s_idle_enabled || s_action_running)
    {
        return;
    }

    const friend_action_t *list = NULL;
    uint32_t count = 0;

    switch (s_current_emotion)
    {
        case FRIEND_EMOTION_NEUTRAL:
            list = IDLE_NEUTRAL;
            count = sizeof(IDLE_NEUTRAL) / sizeof(IDLE_NEUTRAL[0]);
            break;

        case FRIEND_EMOTION_HAPPY:
            list = IDLE_HAPPY;
            count = sizeof(IDLE_HAPPY) / sizeof(IDLE_HAPPY[0]);
            break;

        case FRIEND_EMOTION_MISCHIEVOUS:
            list = IDLE_MISCHIEVOUS;
            count = sizeof(IDLE_MISCHIEVOUS) / sizeof(IDLE_MISCHIEVOUS[0]);
            break;

        case FRIEND_EMOTION_ANGRY:
            list = IDLE_ANGRY;
            count = sizeof(IDLE_ANGRY) / sizeof(IDLE_ANGRY[0]);
            break;

        default:
            return;   // SLEEPY / SURPRISED: dung yen
    }

    friend_ui_play_action(list[esp_random() % count]);
}


// ============================================================
// Public API
// ============================================================

void friend_ui_play_action(friend_action_t action)
{
    if (s_left_eye == NULL || (int)action < 0 || action >= FRIEND_ACTION_COUNT)
    {
        return;
    }

    s_action_def = &s_actions[action];
    s_action_running = true;

    lv_anim_delete(&s_anim_gaze_key, anim_gaze_cb);

    start_anim(&s_anim_action_key, anim_action_cb, 0, 1000, s_action_def->duration_ms, lv_anim_path_linear);
}

void friend_ui_set_idle_actions(bool enable)
{
    s_idle_enabled = enable;
}

static void apply_emotion(const emotion_cfg_t *cfg, bool changed)
{
    s_target_eye_h = cfg->eye_height;
    s_blink_ms = cfg->blink_ms;
    s_eyes_closed = false;

    // ---- Mat: tron <-> cung "^ ^" ----
    if (cfg->arc_eyes)
    {
        lv_obj_add_flag(s_left_eye, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_right_eye, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_left_arc_eye, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_right_arc_eye, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_remove_flag(s_left_eye, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_right_eye, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_left_arc_eye, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_right_arc_eye, LV_OBJ_FLAG_HIDDEN);
    }

    start_anim(&s_anim_eye_key, anim_eye_cb, s_eye_h, cfg->eye_height, 280, lv_anim_path_ease_out);

    // ---- Mieng: cung (cuoi/mieu) hoac "o" ----
    if (cfg->mouth_o)
    {
        lv_obj_add_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_mouth_o, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_remove_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_mouth_o, LV_OBJ_FLAG_HIDDEN);
    }

    lv_arc_set_rotation(s_mouth, (cfg->mouth_rot + 360) % 360);

    start_anim(&s_anim_mouth_key, anim_mouth_cb, s_mouth_v, cfg->mouth_v, 320, lv_anim_path_overshoot);

    // ---- Mi mat ----
    for (int i = 0; i < 4; i++)
    {
        s_lid_from[i] = s_lid_cur[i];
        s_lid_to[i] = cfg->lid[i];
    }

    start_anim(&s_anim_lid_key, anim_lid_cb, 0, 1000, 280, lv_anim_path_ease_out);

    // ---- Ma hong ----
    start_anim(&s_anim_cheek_key, anim_cheek_cb, s_cheek_opa, cfg->cheek_opa, 300, lv_anim_path_ease_out);

    // ---- Zzz ----
    lv_anim_delete(&s_anim_zzz_key, anim_zzz_cb);

    if (cfg->zzz)
    {
        lv_obj_remove_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, &s_anim_zzz_key);
        lv_anim_set_exec_cb(&a, anim_zzz_cb);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_duration(&a, 2600);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_start(&a);
    }
    else
    {
        lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Reset lich chop mat, dua mat ve giua ----
    if (s_current_emotion != FRIEND_EMOTION_NEUTRAL && !s_action_running)
    {
        start_anim(&s_anim_gaze_key, anim_gaze_cb, s_gaze_x, 0, 260, lv_anim_path_ease_out);
    }

    lv_timer_set_period(s_blink_timer, random_range(BLINK_MIN_DELAY_MS, BLINK_MAX_DELAY_MS));
    lv_timer_reset(s_blink_timer);

    // ---- Tu phat "man chao san" cua cam xuc (nhun nhay, giat minh, ...) ----
    if (changed && cfg->entry_action >= 0)
    {
        friend_ui_play_action((friend_action_t)cfg->entry_action);
    }
}

void friend_ui_set_emotion(friend_emotion_t emotion)
{
    if (s_left_eye == NULL || s_mouth == NULL)
    {
        return;
    }

    const emotion_cfg_t *cfg = NULL;

    switch (emotion)
    {
        case FRIEND_EMOTION_NEUTRAL:     cfg = &CFG_NEUTRAL;     ESP_LOGI(TAG, "Emotion: NEUTRAL");     break;
        case FRIEND_EMOTION_HAPPY:       cfg = &CFG_HAPPY;       ESP_LOGI(TAG, "Emotion: HAPPY");       break;
        case FRIEND_EMOTION_SLEEPY:      cfg = &CFG_SLEEPY;      ESP_LOGI(TAG, "Emotion: SLEEPY");      break;
        case FRIEND_EMOTION_ANGRY:       cfg = &CFG_ANGRY;       ESP_LOGI(TAG, "Emotion: ANGRY");       break;
        case FRIEND_EMOTION_SURPRISED:   cfg = &CFG_SURPRISED;   ESP_LOGI(TAG, "Emotion: SURPRISED");   break;
        case FRIEND_EMOTION_MISCHIEVOUS: cfg = &CFG_MISCHIEVOUS; ESP_LOGI(TAG, "Emotion: MISCHIEVOUS"); break;

        default:
            ESP_LOGW(TAG, "Unknown emotion: %d", emotion);
            return;
    }

    bool changed = (emotion != s_current_emotion);

    s_current_emotion = emotion;
    apply_emotion(cfg, changed);
}

esp_err_t friend_ui_init(void)
{
    ESP_LOGI(TAG, "Initializing Hisu UI");

    lv_obj_t *screen = lv_screen_active();

    s_cx = lv_display_get_horizontal_resolution(lv_display_get_default()) / 2;
    s_left_cx = s_cx - (EYE_SPACING / 2);
    s_right_cx = s_cx + (EYE_SPACING / 2);

    // --------------------------------------------------------
    // Background
    // --------------------------------------------------------

    lv_obj_set_style_bg_color(screen, lv_color_hex(BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // --------------------------------------------------------
    // Cheeks
    // --------------------------------------------------------

    s_left_cheek = create_rect(screen, CHEEK_W, CHEEK_H, CHEEK_COLOR, CHEEK_H / 2);
    s_right_cheek = create_rect(screen, CHEEK_W, CHEEK_H, CHEEK_COLOR, CHEEK_H / 2);

    lv_obj_set_style_opa(s_left_cheek, 0, LV_PART_MAIN);
    lv_obj_set_style_opa(s_right_cheek, 0, LV_PART_MAIN);

    // --------------------------------------------------------
    // Round eyes + highlights
    // --------------------------------------------------------

    s_left_eye = create_rect(screen, EYE_WIDTH, EYE_HEIGHT_OPEN, EYE_COLOR, EYE_RADIUS);
    s_right_eye = create_rect(screen, EYE_WIDTH, EYE_HEIGHT_OPEN, EYE_COLOR, EYE_RADIUS);

    lv_obj_t *eyes[2] = { s_left_eye, s_right_eye };

    for (int i = 0; i < 2; i++)
    {
#if ENABLE_GLOW
        lv_obj_set_style_shadow_width(eyes[i], 18, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(eyes[i], lv_color_hex(EYE_COLOR), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(eyes[i], LV_OPA_40, LV_PART_MAIN);
#endif

        lv_obj_t *big = create_rect(eyes[i], 13, 19, HIGHLIGHT_COLOR, 8);
        lv_obj_t *small = create_rect(eyes[i], 7, 7, HIGHLIGHT_COLOR, 4);

        lv_obj_align(big, LV_ALIGN_TOP_LEFT, 9, 9);
        lv_obj_align(small, LV_ALIGN_BOTTOM_RIGHT, -9, -11);

        s_hl[i * 2] = big;
        s_hl[i * 2 + 1] = small;
    }

    // --------------------------------------------------------
    // Happy eyes  ^ ^
    // --------------------------------------------------------

    s_left_arc_eye = create_line_arc(screen, HAPPY_EYE_D, HAPPY_EYE_ARC_W, EYE_COLOR);
    s_right_arc_eye = create_line_arc(screen, HAPPY_EYE_D, HAPPY_EYE_ARC_W, EYE_COLOR);

    lv_arc_set_bg_angles(s_left_arc_eye, 205, 335);
    lv_arc_set_bg_angles(s_right_arc_eye, 205, 335);

    lv_obj_add_flag(s_left_arc_eye, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_right_arc_eye, LV_OBJ_FLAG_HIDDEN);

    // --------------------------------------------------------
    // Eyelids (duong thang day mau nen, che nua tren cua mat)
    // --------------------------------------------------------

    for (int i = 0; i < 2; i++)
    {
        s_lid[i] = lv_line_create(screen);

        lv_obj_set_style_line_width(s_lid[i], LID_T, LV_PART_MAIN);
        lv_obj_set_style_line_color(s_lid[i], lv_color_hex(BACKGROUND_COLOR), LV_PART_MAIN);
        lv_obj_set_style_line_rounded(s_lid[i], false, LV_PART_MAIN);
        lv_obj_add_flag(s_lid[i], LV_OBJ_FLAG_HIDDEN);
    }

    // --------------------------------------------------------
    // Mouth: cung (cuoi/mieu) + mieng tron "o"
    // --------------------------------------------------------

    s_mouth = create_line_arc(screen, MOUTH_D, MOUTH_ARC_W, EYE_COLOR);
    lv_arc_set_bg_angles(s_mouth, 90 - s_mouth_v, 90 + s_mouth_v);

    s_mouth_o = create_rect(screen, 18, 22, EYE_COLOR, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(s_mouth_o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mouth_o, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_mouth_o, lv_color_hex(EYE_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_mouth_o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_mouth_o, LV_OBJ_FLAG_HIDDEN);

    // --------------------------------------------------------
    // Zzz (tao sau cung de luon nam tren mi mat)
    // --------------------------------------------------------

    s_zzz = lv_label_create(screen);

    lv_label_set_text(s_zzz, "z Z");
    lv_obj_set_style_text_color(s_zzz, lv_color_hex(EYE_COLOR), LV_PART_MAIN);
    lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);

    // --------------------------------------------------------
    // Timers
    // --------------------------------------------------------

    layout_all();   // bat dau voi mat nham, sau do mo ra (hieu ung thuc day)

    s_blink_timer = lv_timer_create(blink_timer_cb, random_range(BLINK_MIN_DELAY_MS, BLINK_MAX_DELAY_MS), NULL);
    s_gaze_timer = lv_timer_create(gaze_timer_cb, random_range(GAZE_MIN_DELAY_MS, GAZE_MAX_DELAY_MS), NULL);
    s_idle_timer = lv_timer_create(idle_timer_cb, random_range(IDLE_MIN_DELAY_MS, IDLE_MAX_DELAY_MS), NULL);

    if (s_blink_timer == NULL || s_gaze_timer == NULL || s_idle_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create timers");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Hisu face created - blink, gaze and idle actions enabled");

    friend_ui_set_emotion(FRIEND_EMOTION_NEUTRAL);

    return ESP_OK;
}