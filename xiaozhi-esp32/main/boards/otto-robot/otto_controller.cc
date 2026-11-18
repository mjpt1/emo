/*
    Otto机器人控制器 - MCP协议版本
*/

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>
#include <cstdio>
#include <string>

#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>

#include "application.h"
#include "board.h"
#include "display.h"
#include "audio_codec.h"
#include "assets/lang_config.h"
#include "config.h"
#include "mcp_server.h"
#include "otto_movements.h"
#include "sdkconfig.h"
#include "settings.h"

#define TAG "OttoController"

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    QueueHandle_t action_queue_;
    bool has_hands_ = false;
    bool is_action_in_progress_ = false;

    struct OttoActionParams {
        int action_type;
        int steps;
        int speed;
        int direction;
        int amount;
    };

    enum ActionType {
        ACTION_WALK = 1,
        ACTION_TURN = 2,
        ACTION_JUMP = 3,
        ACTION_SWING = 4,
        ACTION_MOONWALK = 5,
        ACTION_BEND = 6,
        ACTION_SHAKE_LEG = 7,
        ACTION_UPDOWN = 8,
        ACTION_TIPTOE_SWING = 9,
        ACTION_JITTER = 10,
        ACTION_ASCENDING_TURN = 11,
        ACTION_CRUSAITO = 12,
        ACTION_FLAPPING = 13,
        ACTION_HANDS_UP = 14,
        ACTION_HANDS_DOWN = 15,
        ACTION_HAND_WAVE = 16,
        ACTION_HOME = 17
    };

    static void ActionTask(void* arg) {
        OttoController* controller = static_cast<OttoController*>(arg);
        OttoActionParams params;
        controller->otto_.AttachServos();

        while (true) {
            if (xQueueReceive(controller->action_queue_, &params, pdMS_TO_TICKS(1000)) == pdTRUE) {
                ESP_LOGI(TAG, "执行动作: %d", params.action_type);
                controller->is_action_in_progress_ = true;

                switch (params.action_type) {
                    case ACTION_WALK:
                        controller->otto_.Walk(params.steps, params.speed, params.direction,
                                               params.amount);
                        break;
                    case ACTION_TURN:
                        controller->otto_.Turn(params.steps, params.speed, params.direction,
                                               params.amount);
                        break;
                    case ACTION_JUMP:
                        controller->otto_.Jump(params.steps, params.speed);
                        break;
                    case ACTION_SWING:
                        controller->otto_.Swing(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_MOONWALK:
                        controller->otto_.Moonwalker(params.steps, params.speed, params.amount,
                                                     params.direction);
                        break;
                    case ACTION_BEND:
                        controller->otto_.Bend(params.steps, params.speed, params.direction);
                        break;
                    case ACTION_SHAKE_LEG:
                        controller->otto_.ShakeLeg(params.steps, params.speed, params.direction);
                        break;
                    case ACTION_UPDOWN:
                        controller->otto_.UpDown(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_TIPTOE_SWING:
                        controller->otto_.TiptoeSwing(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_JITTER:
                        controller->otto_.Jitter(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_ASCENDING_TURN:
                        controller->otto_.AscendingTurn(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_CRUSAITO:
                        controller->otto_.Crusaito(params.steps, params.speed, params.amount,
                                                   params.direction);
                        break;
                    case ACTION_FLAPPING:
                        controller->otto_.Flapping(params.steps, params.speed, params.amount,
                                                   params.direction);
                        break;
                    case ACTION_HANDS_UP:
                        if (controller->has_hands_) {
                            controller->otto_.HandsUp(params.speed, params.direction);
                        }
                        break;
                    case ACTION_HANDS_DOWN:
                        if (controller->has_hands_) {
                            controller->otto_.HandsDown(params.speed, params.direction);
                        }
                        break;
                    case ACTION_HAND_WAVE:
                        if (controller->has_hands_) {
                            controller->otto_.HandWave(params.speed, params.direction);
                        }
                        break;
                    case ACTION_HOME:
                        controller->otto_.Home(params.direction == 1);
                        break;
                }
                if (params.action_type != ACTION_HOME) {
                    controller->otto_.Home(params.action_type < ACTION_HANDS_UP);
                }
                controller->is_action_in_progress_ = false;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }

    void StartActionTaskIfNeeded() {
        if (action_task_handle_ == nullptr) {
            xTaskCreate(ActionTask, "otto_action", 1024 * 3, this, configMAX_PRIORITIES - 1,
                        &action_task_handle_);
        }
    }

    void QueueAction(int action_type, int steps, int speed, int direction, int amount) {
        // 检查手部动作
        if ((action_type >= ACTION_HANDS_UP && action_type <= ACTION_HAND_WAVE) && !has_hands_) {
            ESP_LOGW(TAG, "尝试执行手部动作，但机器人没有配置手部舵机");
            return;
        }

        ESP_LOGI(TAG, "动作控制: 类型=%d, 步数=%d, 速度=%d, 方向=%d, 幅度=%d", action_type, steps,
                 speed, direction, amount);

        OttoActionParams params = {action_type, steps, speed, direction, amount};
        xQueueSend(action_queue_, &params, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void LoadTrimsFromNVS() {
        Settings settings("otto_trims", false);

        int left_leg = settings.GetInt("left_leg", 0);
        int right_leg = settings.GetInt("right_leg", 0);
        int left_foot = settings.GetInt("left_foot", 0);
        int right_foot = settings.GetInt("right_foot", 0);
        int left_hand = settings.GetInt("left_hand", 0);
        int right_hand = settings.GetInt("right_hand", 0);

        ESP_LOGI(TAG, "从NVS加载微调设置: 左腿=%d, 右腿=%d, 左脚=%d, 右脚=%d, 左手=%d, 右手=%d",
                 left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);

        otto_.SetTrims(left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);
    }

public:
    OttoController() {
        otto_.Init(LEFT_LEG_PIN, RIGHT_LEG_PIN, LEFT_FOOT_PIN, RIGHT_FOOT_PIN, LEFT_HAND_PIN,
                   RIGHT_HAND_PIN);

        has_hands_ = (LEFT_HAND_PIN != -1 && RIGHT_HAND_PIN != -1);
        ESP_LOGI(TAG, "Otto机器人初始化%s手部舵机", has_hands_ ? "带" : "不带");

        LoadTrimsFromNVS();

        action_queue_ = xQueueCreate(20, sizeof(OttoActionParams));

        QueueAction(ACTION_HOME, 1, 1000, 1, 0);  // direction=1表示复位手部

        RegisterMcpTools();

        xTaskCreate(SerialCommandTask, "otto_usb_cmd", 4096, this, 5, nullptr);
    }

    // EMO: USB test commands, no server needed. One line per command, e.g.
    //   otto walk | otto back | otto left | otto right | otto jump | otto swing
    //   otto moonwalk | otto shake | otto updown | otto dance | otto home
    //   otto <type> <steps> <speed> <direction> <amount>   (raw, type = ActionType)
    static void SerialCommandTask(void* arg) {
        auto* self = static_cast<OttoController*>(arg);
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        cfg.rx_buffer_size = 512;
        cfg.tx_buffer_size = 2048;
        if (!usb_serial_jtag_is_driver_installed()) {
            if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
                ESP_LOGE(TAG, "USB cmd: driver install failed");
                vTaskDelete(nullptr);
            }
            usb_serial_jtag_vfs_use_driver();
        }
        ESP_LOGI(TAG, "USB cmd ready: send 'otto walk', 'otto dance', ...");
        char line[96];
        size_t len = 0;
        while (true) {
            uint8_t c;
            if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) continue;
            if (c != '\n' && c != '\r') {
                if (len < sizeof(line) - 1) line[len++] = (char)c;
                continue;
            }
            if (len == 0) continue;
            line[len] = 0;
            len = 0;
            self->HandleSerialCommand(line);
        }
    }

    void HandleSerialCommand(const char* line) {
        char word[16] = {0};
        int a = 0, b = 0, c = 0, d = 0, e = 0;
        if (strncmp(line, "otto ", 5) != 0) return;
        int raw = sscanf(line + 5, "%d %d %d %d %d", &a, &b, &c, &d, &e);
        if (raw == 5) {
            ESP_LOGI(TAG, "USB cmd raw: %s", line);
            QueueAction(a, b, c, d, e);
            return;
        }
        sscanf(line + 5, "%15s", word);
        std::string w(word);
        if (w == "vol") {  // otto vol [0-100]: show / set speaker volume
            auto codec = Board::GetInstance().GetAudioCodec();
            int v = -1;
            sscanf(line + 5, "%*s %d", &v);
            if (v >= 0 && v <= 100) codec->SetOutputVolume(v);
            ESP_LOGW(TAG, "USB cmd: volume=%d", codec->output_volume());
            return;
        }
        if (w == "beep") {  // otto beep: play a test sound
            ESP_LOGW(TAG, "USB cmd: beep (volume=%d)",
                     Board::GetInstance().GetAudioCodec()->output_volume());
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
            return;
        }
        if (w == "eye") {  // otto eye <emotion>: preview an eye expression
            char emo[24] = {0};
            sscanf(line + 5, "%*s %23s", emo);
            ESP_LOGI(TAG, "USB cmd: eye %s", emo);
            Board::GetInstance().GetDisplay()->SetEmotion(emo);
            return;
        }
        ESP_LOGI(TAG, "USB cmd: %s", word);
        if (w == "walk") QueueAction(ACTION_WALK, 4, 1000, 1, 0);
        else if (w == "back") QueueAction(ACTION_WALK, 4, 1000, -1, 0);
        else if (w == "left") QueueAction(ACTION_TURN, 4, 1000, 1, 0);
        else if (w == "right") QueueAction(ACTION_TURN, 4, 1000, -1, 0);
        else if (w == "jump") QueueAction(ACTION_JUMP, 1, 1000, 0, 0);
        else if (w == "swing") QueueAction(ACTION_SWING, 3, 1000, 0, 30);
        else if (w == "moonwalk") QueueAction(ACTION_MOONWALK, 3, 1000, 1, 25);
        else if (w == "shake") QueueAction(ACTION_SHAKE_LEG, 2, 1000, 1, 0);
        else if (w == "updown") QueueAction(ACTION_UPDOWN, 3, 1000, 0, 20);
        else if (w == "home") QueueAction(ACTION_HOME, 1, 1000, 0, 0);
        else if (w == "dance") QueueDance("happy");
        else if (!QueueDance(w)) ESP_LOGW(TAG, "USB cmd unknown: %s", line);
    }

    // EMO: Mochi dance routines (legs + feet only). Each uses <= 10 queued moves.
    bool QueueDance(const std::string& style) {
        ESP_LOGI(TAG, "Mochi dance: %s", style.c_str());
        if (style == "happy") {  // bouncy, cheerful
            QueueAction(ACTION_UPDOWN, 2, 700, 0, 25);
            QueueAction(ACTION_SWING, 3, 700, 0, 30);
            QueueAction(ACTION_TIPTOE_SWING, 3, 700, 0, 25);
            QueueAction(ACTION_JITTER, 4, 500, 0, 20);
            QueueAction(ACTION_SHAKE_LEG, 1, 800, 1, 0);
            QueueAction(ACTION_SHAKE_LEG, 1, 800, -1, 0);
        } else if (style == "calm") {  // slow, smooth
            QueueAction(ACTION_SWING, 3, 1400, 0, 20);
            QueueAction(ACTION_MOONWALK, 3, 1400, 1, 20);
            QueueAction(ACTION_MOONWALK, 3, 1400, -1, 20);
            QueueAction(ACTION_BEND, 1, 1400, 1, 0);
            QueueAction(ACTION_BEND, 1, 1400, -1, 0);
        } else if (style == "party") {  // fast and energetic
            QueueAction(ACTION_JITTER, 4, 500, 0, 25);
            QueueAction(ACTION_CRUSAITO, 3, 800, 1, 25);
            QueueAction(ACTION_CRUSAITO, 3, 800, -1, 25);
            QueueAction(ACTION_FLAPPING, 3, 700, 1, 20);
            QueueAction(ACTION_MOONWALK, 2, 700, 1, 30);
            QueueAction(ACTION_MOONWALK, 2, 700, -1, 30);
            QueueAction(ACTION_ASCENDING_TURN, 2, 800, 0, 25);
            QueueAction(ACTION_JUMP, 1, 600, 0, 0);
        } else if (style == "hello") {  // greeting: little bow and wiggle
            QueueAction(ACTION_BEND, 1, 1000, 1, 0);
            QueueAction(ACTION_TIPTOE_SWING, 2, 800, 0, 20);
            QueueAction(ACTION_SHAKE_LEG, 1, 800, 1, 0);
        } else if (style == "robot") {  // stiff, jerky robot dance
            QueueAction(ACTION_JITTER, 3, 600, 0, 15);
            QueueAction(ACTION_TURN, 1, 700, 1, 0);
            QueueAction(ACTION_JITTER, 3, 600, 0, 15);
            QueueAction(ACTION_TURN, 1, 700, -1, 0);
            QueueAction(ACTION_UPDOWN, 2, 600, 0, 15);
            QueueAction(ACTION_BEND, 1, 800, 1, 0);
        } else if (style == "disco") {  // groovy side-to-side
            QueueAction(ACTION_SWING, 2, 800, 0, 35);
            QueueAction(ACTION_CRUSAITO, 2, 900, 1, 30);
            QueueAction(ACTION_UPDOWN, 2, 700, 0, 25);
            QueueAction(ACTION_CRUSAITO, 2, 900, -1, 30);
            QueueAction(ACTION_TIPTOE_SWING, 3, 700, 0, 30);
            QueueAction(ACTION_SWING, 2, 800, 0, 35);
        } else if (style == "shy") {  // small, cute, embarrassed
            QueueAction(ACTION_TIPTOE_SWING, 2, 1200, 0, 12);
            QueueAction(ACTION_SHAKE_LEG, 1, 1200, 1, 0);
            QueueAction(ACTION_BEND, 1, 1300, -1, 0);
            QueueAction(ACTION_SWING, 2, 1200, 0, 12);
        } else if (style == "crazy") {  // wild and chaotic
            QueueAction(ACTION_JITTER, 5, 500, 0, 30);
            QueueAction(ACTION_JUMP, 1, 500, 0, 0);
            QueueAction(ACTION_FLAPPING, 3, 600, -1, 30);
            QueueAction(ACTION_ASCENDING_TURN, 2, 600, 0, 30);
            QueueAction(ACTION_MOONWALK, 2, 600, 1, 35);
            QueueAction(ACTION_SHAKE_LEG, 1, 600, -1, 0);
            QueueAction(ACTION_JUMP, 1, 500, 0, 0);
        } else if (style == "wave") {  // flowing wave across the body
            QueueAction(ACTION_CRUSAITO, 3, 1100, 1, 25);
            QueueAction(ACTION_CRUSAITO, 3, 1100, -1, 25);
            QueueAction(ACTION_MOONWALK, 2, 1100, 1, 25);
            QueueAction(ACTION_MOONWALK, 2, 1100, -1, 25);
        } else if (style == "ballet") {  // slow, graceful, on tiptoes
            QueueAction(ACTION_TIPTOE_SWING, 3, 1500, 0, 25);
            QueueAction(ACTION_UPDOWN, 2, 1500, 0, 25);
            QueueAction(ACTION_TURN, 2, 1400, 1, 0);
            QueueAction(ACTION_TIPTOE_SWING, 2, 1500, 0, 25);
            QueueAction(ACTION_BEND, 1, 1500, 1, 0);
        } else {
            return false;
        }
        return true;
    }

    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();

        ESP_LOGI(TAG, "开始注册MCP工具...");

        // 基础移动动作
        mcp_server.AddTool("self.otto.walk_forward",
                           "行走。steps: 行走步数(1-100); speed: 行走速度(500-1500，数值越小越快); "
                           "direction: 行走方向(-1=后退, 1=前进); arm_swing: 手臂摆动幅度(0-170度)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("arm_swing", kPropertyTypeInteger, 50, 0, 170),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int arm_swing = properties["arm_swing"].value<int>();
                               int direction = properties["direction"].value<int>();
                               QueueAction(ACTION_WALK, steps, speed, direction, arm_swing);
                               return true;
                           });

        mcp_server.AddTool("self.otto.turn_left",
                           "转身。steps: 转身步数(1-100); speed: 转身速度(500-1500，数值越小越快); "
                           "direction: 转身方向(1=左转, -1=右转); arm_swing: 手臂摆动幅度(0-170度)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("arm_swing", kPropertyTypeInteger, 50, 0, 170),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int arm_swing = properties["arm_swing"].value<int>();
                               int direction = properties["direction"].value<int>();
                               QueueAction(ACTION_TURN, steps, speed, direction, arm_swing);
                               return true;
                           });

        mcp_server.AddTool("self.otto.jump",
                           "跳跃。steps: 跳跃次数(1-100); speed: 跳跃速度(500-1500，数值越小越快)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 1, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               QueueAction(ACTION_JUMP, steps, speed, 0, 0);
                               return true;
                           });

        // 特殊动作
        mcp_server.AddTool("self.otto.swing",
                           "左右摇摆。steps: 摇摆次数(1-100); speed: "
                           "摇摆速度(500-1500，数值越小越快); amount: 摇摆幅度(0-170度)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("amount", kPropertyTypeInteger, 30, 0, 170)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int amount = properties["amount"].value<int>();
                               QueueAction(ACTION_SWING, steps, speed, 0, amount);
                               return true;
                           });

        mcp_server.AddTool("self.otto.moonwalk",
                           "太空步。steps: 太空步步数(1-100); speed: 速度(500-1500，数值越小越快); "
                           "direction: 方向(1=左, -1=右); amount: 幅度(0-170度)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1),
                                         Property("amount", kPropertyTypeInteger, 25, 0, 170)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int direction = properties["direction"].value<int>();
                               int amount = properties["amount"].value<int>();
                               QueueAction(ACTION_MOONWALK, steps, speed, direction, amount);
                               return true;
                           });

        mcp_server.AddTool("self.otto.bend",
                           "弯曲身体。steps: 弯曲次数(1-100); speed: "
                           "弯曲速度(500-1500，数值越小越快); direction: 弯曲方向(1=左, -1=右)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 1, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int direction = properties["direction"].value<int>();
                               QueueAction(ACTION_BEND, steps, speed, direction, 0);
                               return true;
                           });

        mcp_server.AddTool("self.otto.shake_leg",
                           "摇腿。steps: 摇腿次数(1-100); speed: 摇腿速度(500-1500，数值越小越快); "
                           "direction: 腿部选择(1=左腿, -1=右腿)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 1, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int direction = properties["direction"].value<int>();
                               QueueAction(ACTION_SHAKE_LEG, steps, speed, direction, 0);
                               return true;
                           });

        mcp_server.AddTool("self.otto.updown",
                           "上下运动。steps: 上下运动次数(1-100); speed: "
                           "运动速度(500-1500，数值越小越快); amount: 运动幅度(0-170度)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("amount", kPropertyTypeInteger, 20, 0, 170)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int amount = properties["amount"].value<int>();
                               QueueAction(ACTION_UPDOWN, steps, speed, 0, amount);
                               return true;
                           });

        // 手部动作（仅在有手部舵机时可用）
        if (has_hands_) {
            mcp_server.AddTool(
                "self.otto.hands_up",
                "举手。speed: 举手速度(500-1500，数值越小越快); direction: 手部选择(1=左手, "
                "-1=右手, 0=双手)",
                PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                              Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                [this](const PropertyList& properties) -> ReturnValue {
                    int speed = properties["speed"].value<int>();
                    int direction = properties["direction"].value<int>();
                    QueueAction(ACTION_HANDS_UP, 1, speed, direction, 0);
                    return true;
                });

            mcp_server.AddTool(
                "self.otto.hands_down",
                "放手。speed: 放手速度(500-1500，数值越小越快); direction: 手部选择(1=左手, "
                "-1=右手, 0=双手)",
                PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                              Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                [this](const PropertyList& properties) -> ReturnValue {
                    int speed = properties["speed"].value<int>();
                    int direction = properties["direction"].value<int>();
                    QueueAction(ACTION_HANDS_DOWN, 1, speed, direction, 0);
                    return true;
                });

            mcp_server.AddTool(
                "self.otto.hand_wave",
                "挥手。speed: 挥手速度(500-1500，数值越小越快); direction: 手部选择(1=左手, "
                "-1=右手, 0=双手)",
                PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                              Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                [this](const PropertyList& properties) -> ReturnValue {
                    int speed = properties["speed"].value<int>();
                    int direction = properties["direction"].value<int>();
                    QueueAction(ACTION_HAND_WAVE, 1, speed, direction, 0);
                    return true;
                });
        }

        // 系统工具
        mcp_server.AddTool("self.otto.dance",
                           "Mochi performs a full dance routine. Use when the user asks to dance "
                           "(e.g. 'برقص', 'dance'). Pick a different style each time unless the "
                           "user asks for one. style: happy (cheerful, default), calm (slow and "
                           "smooth), party (fast and energetic), hello (greeting / say hi), robot "
                           "(stiff robot dance), disco (groovy), shy (small and cute), crazy "
                           "(wild), wave (flowing), ballet (slow and graceful on tiptoes)",
                           PropertyList({Property("style", kPropertyTypeString, "happy")}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               std::string style = properties["style"].value<std::string>();
                               if (!QueueDance(style)) QueueDance("happy");
                               return true;
                           });

        mcp_server.AddTool("self.otto.stop", "立即停止", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               if (action_task_handle_ != nullptr) {
                                   vTaskDelete(action_task_handle_);
                                   action_task_handle_ = nullptr;
                               }
                               is_action_in_progress_ = false;
                               xQueueReset(action_queue_);

                               QueueAction(ACTION_HOME, 1, 1000, 1, 0);
                               return true;
                           });

        mcp_server.AddTool(
            "self.otto.set_trim",
            "校准单个舵机位置。设置指定舵机的微调参数以调整Otto的初始站立姿态，设置将永久保存。"
            "servo_type: 舵机类型(left_leg/right_leg/left_foot/right_foot/left_hand/right_hand); "
            "trim_value: 微调值(-50到50度)",
            PropertyList({Property("servo_type", kPropertyTypeString, "left_leg"),
                          Property("trim_value", kPropertyTypeInteger, 0, -50, 50)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string servo_type = properties["servo_type"].value<std::string>();
                int trim_value = properties["trim_value"].value<int>();

                ESP_LOGI(TAG, "设置舵机微调: %s = %d度", servo_type.c_str(), trim_value);

                // 获取当前所有微调值
                Settings settings("otto_trims", true);
                int left_leg = settings.GetInt("left_leg", 0);
                int right_leg = settings.GetInt("right_leg", 0);
                int left_foot = settings.GetInt("left_foot", 0);
                int right_foot = settings.GetInt("right_foot", 0);
                int left_hand = settings.GetInt("left_hand", 0);
                int right_hand = settings.GetInt("right_hand", 0);

                // 更新指定舵机的微调值
                if (servo_type == "left_leg") {
                    left_leg = trim_value;
                    settings.SetInt("left_leg", left_leg);
                } else if (servo_type == "right_leg") {
                    right_leg = trim_value;
                    settings.SetInt("right_leg", right_leg);
                } else if (servo_type == "left_foot") {
                    left_foot = trim_value;
                    settings.SetInt("left_foot", left_foot);
                } else if (servo_type == "right_foot") {
                    right_foot = trim_value;
                    settings.SetInt("right_foot", right_foot);
                } else if (servo_type == "left_hand") {
                    if (!has_hands_) {
                        return "错误：机器人没有配置手部舵机";
                    }
                    left_hand = trim_value;
                    settings.SetInt("left_hand", left_hand);
                } else if (servo_type == "right_hand") {
                    if (!has_hands_) {
                        return "错误：机器人没有配置手部舵机";
                    }
                    right_hand = trim_value;
                    settings.SetInt("right_hand", right_hand);
                } else {
                    return "错误：无效的舵机类型，请使用: left_leg, right_leg, left_foot, "
                           "right_foot, left_hand, right_hand";
                }

                otto_.SetTrims(left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);

                QueueAction(ACTION_JUMP, 1, 500, 0, 0);

                return "舵机 " + servo_type + " 微调设置为 " + std::to_string(trim_value) +
                       " 度，已永久保存";
            });

        mcp_server.AddTool("self.otto.get_trims", "获取当前的舵机微调设置", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               Settings settings("otto_trims", false);

                               int left_leg = settings.GetInt("left_leg", 0);
                               int right_leg = settings.GetInt("right_leg", 0);
                               int left_foot = settings.GetInt("left_foot", 0);
                               int right_foot = settings.GetInt("right_foot", 0);
                               int left_hand = settings.GetInt("left_hand", 0);
                               int right_hand = settings.GetInt("right_hand", 0);

                               std::string result =
                                   "{\"left_leg\":" + std::to_string(left_leg) +
                                   ",\"right_leg\":" + std::to_string(right_leg) +
                                   ",\"left_foot\":" + std::to_string(left_foot) +
                                   ",\"right_foot\":" + std::to_string(right_foot) +
                                   ",\"left_hand\":" + std::to_string(left_hand) +
                                   ",\"right_hand\":" + std::to_string(right_hand) + "}";

                               ESP_LOGI(TAG, "获取微调设置: %s", result.c_str());
                               return result;
                           });

        mcp_server.AddTool("self.otto.get_status", "获取机器人状态，返回 moving 或 idle",
                           PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
                               return is_action_in_progress_ ? "moving" : "idle";
                           });

        mcp_server.AddTool("self.battery.get_level", "获取机器人电池电量和充电状态", PropertyList(),
                           [](const PropertyList& properties) -> ReturnValue {
                               auto& board = Board::GetInstance();
                               int level = 0;
                               bool charging = false;
                               bool discharging = false;
                               board.GetBatteryLevel(level, charging, discharging);

                               std::string status =
                                   "{\"level\":" + std::to_string(level) +
                                   ",\"charging\":" + (charging ? "true" : "false") + "}";
                               return status;
                           });

        ESP_LOGI(TAG, "MCP工具注册完成");
    }

    ~OttoController() {
        if (action_task_handle_ != nullptr) {
            vTaskDelete(action_task_handle_);
            action_task_handle_ = nullptr;
        }
        vQueueDelete(action_queue_);
    }
};

static OttoController* g_otto_controller = nullptr;

void InitializeOttoController() {
    if (g_otto_controller == nullptr) {
        g_otto_controller = new OttoController();
        ESP_LOGI(TAG, "Otto控制器已初始化并注册MCP工具");
    }
}
