#include "face.h"

#include <stdbool.h>
#include <stdint.h>

#include "display.h"

#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void draw_eye(
    int center_x,
    int center_y,
    int gaze_x,
    int gaze_y,
    int openness,
    face_expression_t expression
)
{
    const uint16_t eye_white = display_rgb565(235, 245, 255);
    const uint16_t iris_color = display_rgb565(0, 190, 255);
    const uint16_t pupil_color = display_rgb565(0, 8, 15);
    const uint16_t highlight = display_rgb565(255, 255, 255);

    const bool is_left_eye = center_x < (DISPLAY_WIDTH / 2);

    int eye_width = 45;
    int maximum_height = 42;
    int local_openness = openness;
    int local_gaze_x = gaze_x;
    int local_gaze_y = gaze_y;
    int iris_radius = 12;
    int pupil_radius = 6;
    int vertical_offset = 0;

    /*
     * Expressions are created by changing the actual eye shape,
     * pupil size and gaze. No black eyebrow bars are drawn.
     */
    switch (expression)
    {
        case FACE_HAPPY:
            local_openness = (local_openness * 52) / 100;
            vertical_offset = 4;
            local_gaze_y += 2;
            break;

        case FACE_SLEEPY:
            local_openness = (local_openness * 43) / 100;
            vertical_offset = 3;
            local_gaze_y += 2;
            break;

        case FACE_CURIOUS:
            if (is_left_eye)
            {
                maximum_height = 46;
                eye_width = 46;
            }
            else
            {
                local_openness = (local_openness * 72) / 100;
                eye_width = 43;
            }
            local_gaze_x += is_left_eye ? -1 : 1;
            break;

        case FACE_THINKING:
            local_openness = (local_openness * 70) / 100;
            local_gaze_x -= 3;
            local_gaze_y -= 4;
            break;

        case FACE_LISTENING:
            maximum_height = 46;
            eye_width = 47;
            local_gaze_x = 0;
            local_gaze_y = 0;
            iris_radius = 13;
            pupil_radius = 6;
            break;

        case FACE_SPEAKING:
            maximum_height = 44;
            vertical_offset = is_left_eye ? -1 : 1;
            break;

        case FACE_ALERT:
            maximum_height = 49;
            eye_width = 48;
            local_gaze_x = 0;
            local_gaze_y = 0;
            iris_radius = 10;
            pupil_radius = 4;
            break;

        case FACE_NEUTRAL:
        default:
            break;
    }

    if (local_openness > 100)
    {
        local_openness = 100;
    }

    if (local_openness < 0)
    {
        local_openness = 0;
    }

    int eye_height = (maximum_height * local_openness) / 100;

    if (eye_height < 3)
    {
        eye_height = 3;
    }

    int eye_center_y = center_y + vertical_offset;
    int eye_y = eye_center_y - (eye_height / 2);
    int corner_radius = eye_height / 2;

    if (corner_radius > 14)
    {
        corner_radius = 14;
    }

    display_fill_rounded_rectangle(
        center_x - (eye_width / 2),
        eye_y,
        eye_width,
        eye_height,
        corner_radius,
        eye_white
    );

    if (local_openness > 25)
    {
        int horizontal_limit = (eye_width / 2) - iris_radius - 2;
        int vertical_limit = (eye_height / 2) - iris_radius - 1;

        if (horizontal_limit < 0)
        {
            horizontal_limit = 0;
        }

        if (vertical_limit < 0)
        {
            vertical_limit = 0;
        }

        if (local_gaze_x > horizontal_limit)
        {
            local_gaze_x = horizontal_limit;
        }
        else if (local_gaze_x < -horizontal_limit)
        {
            local_gaze_x = -horizontal_limit;
        }

        if (local_gaze_y > vertical_limit)
        {
            local_gaze_y = vertical_limit;
        }
        else if (local_gaze_y < -vertical_limit)
        {
            local_gaze_y = -vertical_limit;
        }

        int pupil_center_x = center_x + local_gaze_x;
        int pupil_center_y = eye_center_y + local_gaze_y;

        display_fill_circle(
            pupil_center_x,
            pupil_center_y,
            iris_radius,
            iris_color
        );

        display_fill_circle(
            pupil_center_x,
            pupil_center_y,
            pupil_radius,
            pupil_color
        );

        display_fill_circle(
            pupil_center_x - 4,
            pupil_center_y - 4,
            2,
            highlight
        );
    }
}

void face_draw(
    int gaze_x,
    int gaze_y,
    int openness,
    face_expression_t expression
)
{
    const uint16_t background = display_rgb565(0, 5, 12);

    display_clear(background);

    draw_eye(
        36,
        64,
        gaze_x,
        gaze_y,
        openness,
        expression
    );

    draw_eye(
        92,
        64,
        gaze_x,
        gaze_y,
        openness,
        expression
    );

    display_present();
}

void face_animate_gaze(
    int start_x,
    int start_y,
    int end_x,
    int end_y,
    int frames,
    face_expression_t expression
)
{
    const int saccade_frames = 3;

    for (int frame = 0; frame <= saccade_frames; ++frame)
    {
        int gaze_x = start_x + ((end_x - start_x) * frame) / saccade_frames;
        int gaze_y = start_y + ((end_y - start_y) * frame) / saccade_frames;

        face_draw(gaze_x, gaze_y, 100, expression);
        vTaskDelay(pdMS_TO_TICKS(18));
    }

    if ((esp_random() % 4U) == 0U)
    {
        int adjust_x = end_x + ((int)(esp_random() % 3U) - 1);
        int adjust_y = end_y + ((int)(esp_random() % 3U) - 1);

        face_draw(adjust_x, adjust_y, 100, expression);
        vTaskDelay(pdMS_TO_TICKS(25));
        face_draw(end_x, end_y, 100, expression);
    }
}

void face_animate_blink(
    int gaze_x,
    int gaze_y,
    face_expression_t expression
)
{
    for (int openness = 100; openness >= 0; openness -= 20)
    {
        face_draw(
            gaze_x,
            gaze_y,
            openness,
            expression
        );

        vTaskDelay(pdMS_TO_TICKS(28));
    }

    vTaskDelay(pdMS_TO_TICKS(45));

    for (int openness = 0; openness <= 100; openness += 20)
    {
        face_draw(
            gaze_x,
            gaze_y,
            openness,
            expression
        );

        vTaskDelay(pdMS_TO_TICKS(28));
    }
}
