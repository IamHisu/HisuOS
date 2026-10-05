#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Trang thai cam xuc (giu nguyen cho toi khi doi)
typedef enum
{
    FRIEND_EMOTION_NEUTRAL = 0,
    FRIEND_EMOTION_HAPPY,
    FRIEND_EMOTION_SLEEPY,
    FRIEND_EMOTION_ANGRY,        // gian, mi mat cau xuong
    FRIEND_EMOTION_SURPRISED,    // mat to, mieng "o"
    FRIEND_EMOTION_MISCHIEVOUS,  // tinh nghich, nhech mieng, nhuong may
} friend_emotion_t;

// Hanh dong 1 lan (phat xong tu tro ve binh thuong), kieu Cozmo/Emo
typedef enum
{
    FRIEND_ACTION_SHAKE = 0,     // lac dau "khong"
    FRIEND_ACTION_NOD,           // gat dau "co"
    FRIEND_ACTION_BOUNCE,        // nhun nhay vui ve (squash & stretch)
    FRIEND_ACTION_WINK,          // nhay mat phai
    FRIEND_ACTION_LOOK_AROUND,   // liec quanh to mo
    FRIEND_ACTION_STARTLE,       // giat minh
    FRIEND_ACTION_GIGGLE,        // cuoi khuc khich, rung nguoi
    FRIEND_ACTION_SNEAKY,        // liec len roi chop mat
    FRIEND_ACTION_COUNT
} friend_action_t;

esp_err_t friend_ui_init(void);

void friend_ui_set_emotion(friend_emotion_t emotion);

void friend_ui_play_action(friend_action_t action);

// Bat/tat viec tu lam tro ngau nhien khi ranh (mac dinh: bat)
void friend_ui_set_idle_actions(bool enable);

#ifdef __cplusplus
}
#endif