#pragma once

typedef enum
{
    FACE_NEUTRAL,
    FACE_HAPPY,
    FACE_SLEEPY,
    FACE_CURIOUS,
    FACE_THINKING,
    FACE_LISTENING,
    FACE_SPEAKING,
    FACE_ALERT
} face_expression_t;

void face_draw(
    int gaze_x,
    int gaze_y,
    int openness,
    face_expression_t expression
);

void face_animate_gaze(
    int start_x,
    int start_y,
    int end_x,
    int end_y,
    int frames,
    face_expression_t expression
);

void face_animate_blink(
    int gaze_x,
    int gaze_y,
    face_expression_t expression
);
