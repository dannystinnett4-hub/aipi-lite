#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    FACE_COMMAND_AUTONOMOUS,
    FACE_COMMAND_IDLE,
    FACE_COMMAND_CURIOUS,
    FACE_COMMAND_THINKING,
    FACE_COMMAND_LISTENING,
    FACE_COMMAND_HAPPY,
    FACE_COMMAND_SLEEPY,
    FACE_COMMAND_SPEAKING,
    FACE_COMMAND_ALERT
} face_command_t;

void behavior_initialize(void);
void behavior_start(void);

bool face_send_command(
    face_command_t command,
    uint32_t timeout_ms
);
