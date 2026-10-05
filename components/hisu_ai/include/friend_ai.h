#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define FRIEND_AI_KEY_MAX 512
#define FRIEND_AI_QUESTION_MAX 600
#define FRIEND_AI_NAME_MAX 96
#define FRIEND_AI_PERSONALITY_MAX 512

typedef struct
{
    char name[FRIEND_AI_NAME_MAX + 1];
    char personality[FRIEND_AI_PERSONALITY_MAX + 1];
} friend_ai_profile_t;

typedef enum
{
    FRIEND_AI_GEMINI,
    FRIEND_AI_OPENAI,
} friend_ai_provider_t;

typedef enum
{
    FRIEND_AI_IDLE,
    FRIEND_AI_WORKING,
    FRIEND_AI_READY,
    FRIEND_AI_SHOWING,
    FRIEND_AI_DONE,
    FRIEND_AI_ERROR,
} friend_ai_state_t;

typedef struct {
    // Called from request/worker tasks; must only inspect thread-safe state.
    bool (*available)(void);
    // These three callbacks are called only by the presentation owner task.
    bool (*show)(const char *text);
    bool (*active)(void);
    void (*clear)(void);
} friend_ai_output_t;

// Register before init; callbacks must remain valid for the application lifetime.
esp_err_t friend_ai_set_output(const friend_ai_output_t *output);

esp_err_t friend_ai_init(void);
friend_ai_provider_t friend_ai_get_provider(void);
esp_err_t friend_ai_set_provider(friend_ai_provider_t provider);
const char *friend_ai_provider_name(void);
esp_err_t friend_ai_save_key(const char *key);
bool friend_ai_has_key(void);
void friend_ai_get_profile(friend_ai_profile_t *profile);
esp_err_t friend_ai_save_profile(const friend_ai_profile_t *profile);
esp_err_t friend_ai_submit(const char *question);
esp_err_t friend_ai_test_subtitle(void);
friend_ai_state_t friend_ai_state(void);
const char *friend_ai_error(void);
void friend_ai_process_ui(void);
void friend_ai_cancel(void);
