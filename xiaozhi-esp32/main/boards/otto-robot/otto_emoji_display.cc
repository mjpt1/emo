#include "otto_emoji_display.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "application.h"
#include "assets/lang_config.h"

#define TAG "MochiEyes"

namespace {
constexpr uint32_t kBg = 0x000000;       // screen background (also lid colour)
constexpr uint32_t kEye = 0xA8F5DF;      // soft mint-green eye
constexpr uint32_t kShine = 0xD9233A;    // red pupil, matches the robot body
constexpr uint32_t kHeart = 0xFF8FB1;    // love eyes
constexpr int kEyeGap = 54;              // eye centre offset from screen centre
constexpr float kScaleW = 1.65f;         // big oval eyes that almost touch
constexpr float kScaleH = 1.45f;
constexpr int kPupilW = 13, kPupilH = 17;
constexpr int kTiltDeg10 = 120;          // eye tilt in 0.1 deg (12 degrees)
constexpr int kFrameMs = 33;             // ~30 fps target, light on CPU
constexpr uint32_t kEyeTop = 0xE2FFF5;   // 3D shading: light from above
constexpr uint32_t kEyeBottom = 0x5ECFB0;
constexpr uint32_t kEyeEdge = 0x3FA88C;
constexpr uint32_t kPupilTop = 0xFF5468;
constexpr uint32_t kPupilBottom = 0x8E0F22;

float Approach(float cur, float target, float k) { return cur + (target - cur) * k; }
int RandRange(int lo, int hi) { return lo + (int)(esp_random() % (uint32_t)(hi - lo + 1)); }
int64_t NowMs() { return esp_timer_get_time() / 1000; }
}  // namespace

OttoEmojiDisplay::OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                   int width, int height, int offset_x, int offset_y, bool mirror_x,
                                   bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
    SetupEyes();
}

void OttoEmojiDisplay::SetupEyes() {
    DisplayLockGuard lock(this);

    if (emoji_label_) { lv_obj_del(emoji_label_); emoji_label_ = nullptr; }
    if (chat_message_label_) { lv_obj_del(chat_message_label_); chat_message_label_ = nullptr; }
    if (content_) { lv_obj_del(content_); content_ = nullptr; }

    lv_obj_set_style_bg_color(container_, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);

    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(content_, LV_HOR_RES, LV_HOR_RES);
    lv_obj_set_style_bg_color(content_, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    // emoji_label_ is used by the base class; keep a hidden one around.
    emoji_label_ = lv_label_create(content_);
    lv_label_set_text(emoji_label_, "");
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    eyes_box_ = lv_obj_create(content_);
    lv_obj_remove_style_all(eyes_box_);
    lv_obj_set_size(eyes_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(eyes_box_);
    lv_obj_remove_flag(eyes_box_, LV_OBJ_FLAG_SCROLLABLE);

    for (Eye* e : {&left_, &right_}) {
        e->eye = lv_obj_create(eyes_box_);
        lv_obj_remove_style_all(e->eye);
        // simple flat eye (light to render)
        lv_obj_set_style_bg_color(e->eye, lv_color_hex(kEye), 0);
        lv_obj_set_style_bg_opa(e->eye, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(e->eye, 30, 0);
        (void)kEyeTop; (void)kEyeBottom; (void)kEyeEdge;
        // no clip_corner: lids are black on a black background, clipping is not needed
        // and it made rendering so heavy that audio playback stopped working.
        lv_obj_remove_flag(e->eye, LV_OBJ_FLAG_SCROLLABLE);

        e->shine = lv_obj_create(e->eye);
        lv_obj_remove_style_all(e->shine);
        lv_obj_set_style_bg_color(e->shine, lv_color_hex(kShine), 0);
        (void)kPupilTop; (void)kPupilBottom;
        lv_obj_set_style_bg_opa(e->shine, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(e->shine, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(e->shine, kPupilW, kPupilH);
        lv_obj_remove_flag(e->shine, LV_OBJ_FLAG_SCROLLABLE);

        e->catch_light = lv_obj_create(e->shine);
        lv_obj_remove_style_all(e->catch_light);
        lv_obj_set_style_bg_color(e->catch_light, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(e->catch_light, LV_OPA_90, 0);
        lv_obj_set_style_radius(e->catch_light, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(e->catch_light, 4, 4);
        lv_obj_set_pos(e->catch_light, 3, 3);
        lv_obj_add_flag(e->catch_light, LV_OBJ_FLAG_HIDDEN);  // simple style

        // glassy highlight (upper area), drawn above the pupil
        e->glint = lv_obj_create(e->eye);
        lv_obj_remove_style_all(e->glint);
        lv_obj_set_style_bg_color(e->glint, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(e->glint, LV_OPA_50, 0);
        lv_obj_set_style_radius(e->glint, LV_RADIUS_CIRCLE, 0);

        for (lv_obj_t** lid : {&e->top_lid, &e->bottom_lid}) {
            *lid = lv_obj_create(e->eye);
            lv_obj_remove_style_all(*lid);
            lv_obj_set_style_bg_color(*lid, lv_color_hex(kBg), 0);
            lv_obj_set_style_bg_opa(*lid, LV_OPA_COVER, 0);
            lv_obj_remove_flag(*lid, LV_OBJ_FLAG_SCROLLABLE);
        }
        lv_obj_set_style_radius(e->bottom_lid, LV_RADIUS_CIRCLE, 0);
    }

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(chat_message_label_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(chat_message_label_, lv_color_black(), 0);
    lv_obj_set_style_pad_ver(chat_message_label_, 5, 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

    int64_t now = NowMs();
    next_blink_ms_ = now + 2000;
    next_look_ms_ = now + 1500;
    timer_ = lv_timer_create(AnimTimerCb, kFrameMs, this);
}

void OttoEmojiDisplay::AnimTimerCb(lv_timer_t* t) {
    static_cast<OttoEmojiDisplay*>(lv_timer_get_user_data(t))->Animate();
}

OttoEmojiDisplay::EyeShape OttoEmojiDisplay::TargetShape(int64_t now) {
    EyeShape s = {60, 80, 0, 0, 0, 0, 0};
    switch (mood_) {
        case MOOD_HAPPY:     s = {64, 74, 0.00f, 0, 0.55f, 0, 0}; break;
        case MOOD_LOVE:      s = {64, 74, 0.00f, 0, 0.45f, 0, 0}; break;
        case MOOD_SAD:       s = {58, 74, 0.35f, 22, 0, 0, 6}; break;
        case MOOD_ANGRY:     s = {60, 70, 0.40f, -24, 0, 0, 0}; break;
        case MOOD_SURPRISED: s = {72, 92, 0, 0, 0, 0, 0}; break;
        case MOOD_THINKING:  s = {56, 64, 0.25f, 0, 0, -16, -14}; break;
        case MOOD_SLEEPY:    s = {62, 80, 0.58f, 6, 0, 0, 6}; break;
        default: break;
    }

    if (activity_ == ACT_LISTENING) {
        // attentive: slightly bigger, looking at the user, pulsing with voice
        float boost = 1.06f + 0.14f * voice_level_;
        s.w *= boost;
        s.h *= boost;
        // still glance around a little while listening
        if (now >= next_look_ms_) {
            look_tx_ = RandRange(-8, 8); look_ty_ = RandRange(-5, 5);
            next_look_ms_ = now + RandRange(1500, 3500);
        }
        s.look_x = look_tx_;
        s.look_y = look_ty_;
        return s;
    }

    if (activity_ == ACT_SPEAKING && mood_ == MOOD_NEUTRAL)
        s.bottom_lid = std::max(s.bottom_lid, 0.18f);  // smiling eyes

    // look around: different places, sometimes back to the centre
    if (now >= next_look_ms_) {
        int r = RandRange(0, 5);
        if (r == 0) { look_tx_ = 0; look_ty_ = 0; }
        else {
            int range_x = activity_ == ACT_SPEAKING ? 14 : 26;
            int range_y = activity_ == ACT_SPEAKING ? 8 : 16;
            look_tx_ = RandRange(-range_x, range_x);
            look_ty_ = RandRange(-range_y, range_y);
        }
        next_look_ms_ = now + RandRange(1800, 4500);
    }
    if (mood_ != MOOD_SLEEPY) {
        s.look_x += look_tx_;
        s.look_y += look_ty_;
    }
    if (activity_ == ACT_IDLE && voice_level_ > 0.5f) {  // heard something: perk up
        s.w *= 1.12f; s.h *= 1.12f; s.top_lid = 0; s.look_x = 0; s.look_y = -4;
    }
    return s;
}

void OttoEmojiDisplay::Animate() {
    int64_t now = NowMs();

    // frame-rate independent speed: scale easing by real elapsed time
    static int64_t last_ms = 0;
    float dt = last_ms ? (float)(now - last_ms) : (float)kFrameMs;
    last_ms = now;
    dt = std::min(dt, 120.0f);
    float tf = dt / 16.0f;  // 1.0 == one 16 ms tick
    static int frames = 0;
    static int64_t fps_t0 = now;
    if (++frames, now - fps_t0 >= 10000) {
        ESP_LOGI(TAG, "eyes fps=%.1f", frames * 1000.0f / (now - fps_t0));
        frames = 0;
        fps_t0 = now;
    }

    bool voice = Application::GetInstance().IsVoiceDetected();
    voice_level_ = Approach(voice_level_, voice ? 1.0f : 0.0f, voice ? 0.35f : 0.08f);

    EyeShape t = TargetShape(now);
    // smooth but lively: ~0.25 s to settle a shape, ~0.35 s for a glance
    float k = 1.0f - std::pow(1.0f - 0.16f, tf);
    float kl = 1.0f - std::pow(1.0f - 0.11f, tf);
    cur_.w = Approach(cur_.w, t.w, k);
    cur_.h = Approach(cur_.h, t.h, k);
    cur_.top_lid = Approach(cur_.top_lid, t.top_lid, k);
    cur_.lid_angle = Approach(cur_.lid_angle, t.lid_angle, k);
    cur_.bottom_lid = Approach(cur_.bottom_lid, t.bottom_lid, k);
    cur_.look_x = Approach(cur_.look_x, t.look_x, kl);
    cur_.look_y = Approach(cur_.look_y, t.look_y, kl);

    // blinking (not while sleepy-closed or happy-squint)
    float openness = 1.0f;
    if (blink_start_ms_ < 0 && now >= next_blink_ms_) blink_start_ms_ = now;
    if (blink_start_ms_ >= 0) {
        float p = (now - blink_start_ms_) / 170.0f;  // quick, natural blink
        if (p >= 1.0f) {
            blink_start_ms_ = -1;
            // sometimes a quick double blink
            next_blink_ms_ = now + (RandRange(0, 5) == 0 ? 180 : RandRange(2200, 5500));
        } else {
            float c = std::cos(p * 2.0f * (float)M_PI);  // smooth close/open curve
            openness = 0.5f + 0.5f * c;
            openness = std::max(openness, 0.06f);
        }
    }

    // tiny, slow pupil wander so the eyes feel alive even when still
    if (now >= next_jitter_ms_) {
        jitter_tx_ = RandRange(-3, 3);
        jitter_ty_ = RandRange(-2, 2);
        next_jitter_ms_ = now + RandRange(400, 1200);
    }
    float kj = 1.0f - std::pow(1.0f - 0.10f, tf);
    pupil_jx_ = Approach(pupil_jx_, jitter_tx_, kj);
    pupil_jy_ = Approach(pupil_jy_, jitter_ty_, kj);

    float dy = 0;
    if (activity_ == ACT_SPEAKING) dy = -3.0f * (0.5f + 0.5f * std::sin(now / 160.0f));

    DisplayLockGuard lock(this);
    ApplyEye(left_, -1, cur_, openness, dy);
    ApplyEye(right_, 1, cur_, openness, dy);
}

void OttoEmojiDisplay::ApplyEye(Eye& e, int side, const EyeShape& s, float openness, float dy) {
    // perspective: looking sideways narrows the eyes; the eye on the side we
    // look towards is a bit closer (bigger), the other a bit further away
    float persp = 1.0f, squash = 1.0f, vsquash = 1.0f;  // simple style: no perspective
    int w = (int)(s.w * kScaleW * persp * squash);
    int full_h = (int)(s.h * kScaleH * persp * vsquash);
    int h = std::max(4, (int)(full_h * openness));
    lv_obj_set_size(e.eye, w, h);
    lv_obj_set_style_radius(e.eye, LV_RADIUS_CIRCLE, 0);
    // the whole eye moves with the gaze, the pupil moves further inside it
    lv_obj_align(e.eye, LV_ALIGN_CENTER, side * kEyeGap + (int)(s.look_x * 0.55f),
                 (int)(s.look_y * 0.55f + dy) - 6);
    // tilted ovals: top of each eye leans outward (mirrored left/right)
    lv_obj_set_style_transform_pivot_x(e.eye, w / 2, 0);
    lv_obj_set_style_transform_pivot_y(e.eye, h / 2, 0);
    lv_obj_set_style_transform_rotation(e.eye, 0, 0);  // tilt disabled: rotating the eye costs ~6 fps
    (void)kTiltDeg10;

    // upper lid: wide rect whose bottom edge sits at top_lid * h, rotated for mood
    int lid_w = w * 2;
    int lid_h = h;
    lv_obj_set_size(e.top_lid, lid_w, lid_h);
    lv_obj_set_pos(e.top_lid, (w - lid_w) / 2, (int)(s.top_lid * h) - lid_h);
    lv_obj_set_style_transform_pivot_x(e.top_lid, lid_w / 2, 0);
    lv_obj_set_style_transform_pivot_y(e.top_lid, lid_h, 0);
    // left eye: outer side is left; positive angle lowers the outer side
    int angle10 = (int)(s.lid_angle * 10) * (side < 0 ? -1 : 1);
    lv_obj_set_style_transform_rotation(e.top_lid, angle10, 0);
    if (s.top_lid < 0.01f && std::fabs(s.lid_angle) < 0.5f) lv_obj_add_flag(e.top_lid, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(e.top_lid, LV_OBJ_FLAG_HIDDEN);

    // lower lid: big circle rising from below -> "^" happy eyes
    int bl = (int)(w * 1.5f);
    lv_obj_set_size(e.bottom_lid, bl, bl);
    lv_obj_set_pos(e.bottom_lid, (w - bl) / 2, h - (int)(s.bottom_lid * h));
    if (s.bottom_lid < 0.01f) lv_obj_add_flag(e.bottom_lid, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(e.bottom_lid, LV_OBJ_FLAG_HIDDEN);

    // small red pupil, sitting towards the inner side (cute, slightly cross-eyed)
    float px = w / 2.0f - side * w * 0.12f + s.look_x * 1.4f + pupil_jx_;
    float py = h / 2.0f + (s.look_y * 1.4f + pupil_jy_) * openness;
    int pw = kPupilW, ph = std::max(2, (int)(kPupilH * std::min(1.0f, openness * 1.3f)));
    // keep the pupil inside the eye
    px = std::min(std::max(px, pw * 0.5f + 6.0f), w - pw * 0.5f - 6.0f);
    py = std::min(std::max(py, ph * 0.5f + 6.0f), h - ph * 0.5f - 6.0f);
    lv_obj_set_size(e.shine, pw, ph);
    lv_obj_set_pos(e.shine, (int)(px - pw / 2.0f), (int)(py - ph / 2.0f));
    if (h < 14) lv_obj_add_flag(e.shine, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(e.shine, LV_OBJ_FLAG_HIDDEN);

    // glint sits up-left and drifts opposite to the gaze (parallax = depth)
    int gw = std::max(6, (int)(w * 0.30f));
    int gh = std::max(4, (int)(h * 0.16f));
    lv_obj_set_size(e.glint, gw, gh);
    lv_obj_set_pos(e.glint, (int)(w * 0.20f - s.look_x * 0.35f),
                   (int)(h * 0.12f - s.look_y * 0.25f));
    lv_obj_add_flag(e.glint, LV_OBJ_FLAG_HIDDEN);  // simple style: no glint

    lv_obj_set_style_bg_color(e.eye, lv_color_hex(mood_ == MOOD_LOVE ? kHeart : kEye), 0);
}

void OttoEmojiDisplay::SetEmotion(const char* emotion) {
    if (!emotion) return;
    struct { const char* name; Mood mood; } maps[] = {
        {"neutral", MOOD_NEUTRAL}, {"relaxed", MOOD_NEUTRAL},
        {"happy", MOOD_HAPPY}, {"laughing", MOOD_HAPPY}, {"funny", MOOD_HAPPY},
        {"confident", MOOD_HAPPY}, {"winking", MOOD_HAPPY}, {"cool", MOOD_HAPPY},
        {"delicious", MOOD_HAPPY}, {"silly", MOOD_HAPPY},
        {"loving", MOOD_LOVE}, {"kissy", MOOD_LOVE},
        {"sad", MOOD_SAD}, {"crying", MOOD_SAD},
        {"angry", MOOD_ANGRY},
        {"surprised", MOOD_SURPRISED}, {"shocked", MOOD_SURPRISED},
        {"thinking", MOOD_THINKING}, {"confused", MOOD_THINKING}, {"embarrassed", MOOD_THINKING},
        {"sleepy", MOOD_SLEEPY},
    };
    Mood m = MOOD_NEUTRAL;
    for (auto& e : maps) {
        if (strcmp(e.name, emotion) == 0) { m = e.mood; break; }
    }
    mood_ = m;
    ESP_LOGI(TAG, "emotion %s -> %d", emotion, (int)m);
}

void OttoEmojiDisplay::SetStatus(const char* status) {
    SpiLcdDisplay::SetStatus(status);
    if (!status) return;
    if (strcmp(status, Lang::Strings::LISTENING) == 0) activity_ = ACT_LISTENING;
    else if (strcmp(status, Lang::Strings::SPEAKING) == 0) activity_ = ACT_SPEAKING;
    else if (strcmp(status, Lang::Strings::STANDBY) == 0) activity_ = ACT_IDLE;

    bool ready = strcmp(status, Lang::Strings::STANDBY) == 0 ||
                 strcmp(status, Lang::Strings::LISTENING) == 0 ||
                 strcmp(status, Lang::Strings::SPEAKING) == 0;
    if (ready && !connected_once_ && status_bar_ != nullptr) {
        connected_once_ = true;
        DisplayLockGuard lock(this);
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);  // hide clock / WiFi after first connect
    }
}

void OttoEmojiDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) return;
    if (content == nullptr || strlen(content) == 0) {
        lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(chat_message_label_, content);
    lv_obj_remove_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
}
