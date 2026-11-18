#pragma once

#include "display/lcd_display.h"

/**
 * Mochi animated eyes (replaces the Otto GIF emojis).
 * Two drawn eyes that blink, look around, show emotions, react to voice
 * while listening and bounce while speaking. The status bar (clock / WiFi)
 * is hidden once the robot is connected.
 */
class OttoEmojiDisplay : public SpiLcdDisplay {
public:
    OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy);

    virtual ~OttoEmojiDisplay() = default;

    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetStatus(const char* status) override;

private:
    enum Mood { MOOD_NEUTRAL, MOOD_HAPPY, MOOD_SAD, MOOD_ANGRY, MOOD_SURPRISED,
                MOOD_THINKING, MOOD_SLEEPY, MOOD_LOVE };
    enum Activity { ACT_IDLE, ACT_LISTENING, ACT_SPEAKING };

    struct EyeShape {
        float w, h;       // eye size (px)
        float top_lid;    // 0..1 how much the upper lid covers
        float lid_angle;  // deg, >0 outer side lower (sad), <0 inner side lower (angry)
        float bottom_lid; // 0..1 curved lower lid (happy "^" eyes)
        float look_x, look_y;
    };

    struct Eye {
        lv_obj_t* eye = nullptr;
        lv_obj_t* top_lid = nullptr;
        lv_obj_t* bottom_lid = nullptr;
        lv_obj_t* shine = nullptr;   // red pupil
        lv_obj_t* glint = nullptr;   // glassy highlight on the eye
        lv_obj_t* catch_light = nullptr;  // tiny white dot on the pupil
    };

    void SetupEyes();
    static void AnimTimerCb(lv_timer_t* t);
    void Animate();
    void ApplyEye(Eye& e, int side, const EyeShape& s, float openness, float dy);
    EyeShape TargetShape(int64_t now_ms);

    Eye left_, right_;
    lv_obj_t* eyes_box_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    volatile Mood mood_ = MOOD_NEUTRAL;
    volatile Activity activity_ = ACT_IDLE;
    bool connected_once_ = false;

    EyeShape cur_ = {60, 80, 0, 0, 0, 0, 0};
    int64_t next_blink_ms_ = 0;
    int64_t blink_start_ms_ = -1;
    int64_t next_look_ms_ = 0;
    float look_tx_ = 0, look_ty_ = 0;
    int64_t next_jitter_ms_ = 0;
    float jitter_tx_ = 0, jitter_ty_ = 0;
    float pupil_jx_ = 0, pupil_jy_ = 0;
    float voice_level_ = 0;
};
