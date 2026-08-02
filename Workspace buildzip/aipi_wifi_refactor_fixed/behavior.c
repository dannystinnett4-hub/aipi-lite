#include "behavior.h"

#include "face.h"

#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "AIPI_BEHAVIOR";

typedef enum
{
    BEHAVIOR_IDLE,
    BEHAVIOR_CURIOUS,
    BEHAVIOR_THINKING,
    BEHAVIOR_LISTENING,
    BEHAVIOR_HAPPY,
    BEHAVIOR_SLEEPY,
    BEHAVIOR_ALERT
} behavior_state_t;

static behavior_state_t choose_next_behavior(
    behavior_state_t current_behavior
)
{
    uint32_t roll = esp_random() % 100U;
    behavior_state_t next_behavior;

    if (roll < 38U)
    {
        next_behavior = BEHAVIOR_IDLE;
    }
    else if (roll < 54U)
    {
        next_behavior = BEHAVIOR_CURIOUS;
    }
    else if (roll < 68U)
    {
        next_behavior = BEHAVIOR_THINKING;
    }
    else if (roll < 79U)
    {
        next_behavior = BEHAVIOR_LISTENING;
    }
    else if (roll < 89U)
    {
        next_behavior = BEHAVIOR_HAPPY;
    }
    else if (roll < 96U)
    {
        next_behavior = BEHAVIOR_SLEEPY;
    }
    else
    {
        next_behavior = BEHAVIOR_ALERT;
    }

    if (
        next_behavior == current_behavior &&
        current_behavior != BEHAVIOR_IDLE
    )
    {
        next_behavior = BEHAVIOR_IDLE;
    }

    return next_behavior;
}

static face_expression_t behavior_expression(
    behavior_state_t behavior
)
{
    switch (behavior)
    {
        case BEHAVIOR_CURIOUS:
            return FACE_CURIOUS;

        case BEHAVIOR_THINKING:
            return FACE_THINKING;

        case BEHAVIOR_LISTENING:
            return FACE_LISTENING;

        case BEHAVIOR_HAPPY:
            return FACE_HAPPY;

        case BEHAVIOR_SLEEPY:
            return FACE_SLEEPY;

        case BEHAVIOR_ALERT:
            return FACE_ALERT;

        case BEHAVIOR_IDLE:
        default:
            return FACE_NEUTRAL;
    }
}

static uint32_t behavior_duration_ms(
    behavior_state_t behavior
)
{
    switch (behavior)
    {
        case BEHAVIOR_CURIOUS:
            return 4500U + (esp_random() % 3500U);

        case BEHAVIOR_THINKING:
            return 5000U + (esp_random() % 4000U);

        case BEHAVIOR_LISTENING:
            return 3500U + (esp_random() % 3000U);

        case BEHAVIOR_HAPPY:
            return 3000U + (esp_random() % 3000U);

        case BEHAVIOR_SLEEPY:
            return 6000U + (esp_random() % 5000U);

        case BEHAVIOR_ALERT:
            return 1800U + (esp_random() % 1600U);

        case BEHAVIOR_IDLE:
        default:
            return 5000U + (esp_random() % 5000U);
    }
}

static void choose_behavior_gaze(
    behavior_state_t behavior,
    int *target_x,
    int *target_y
)
{
    uint32_t random_value = esp_random();

    switch (behavior)
    {
        case BEHAVIOR_CURIOUS:
            *target_x = ((random_value & 1U) != 0U) ? 8 : -8;
            *target_y = (int)((random_value >> 8) % 7U) - 3;
            break;

        case BEHAVIOR_THINKING:
            *target_x = -5 + (int)(random_value % 4U);
            *target_y = -4 + (int)((random_value >> 8) % 3U);
            break;

        case BEHAVIOR_LISTENING:
            *target_x = (int)(random_value % 5U) - 2;
            *target_y = (int)((random_value >> 8) % 3U) - 1;
            break;

        case BEHAVIOR_HAPPY:
            *target_x = (int)(random_value % 17U) - 8;
            *target_y = (int)((random_value >> 8) % 7U) - 3;
            break;

        case BEHAVIOR_SLEEPY:
            *target_x = (int)(random_value % 9U) - 4;
            *target_y = 2 + (int)((random_value >> 8) % 3U);
            break;

        case BEHAVIOR_ALERT:
            *target_x = (int)(random_value % 17U) - 8;
            *target_y = (int)((random_value >> 8) % 9U) - 4;
            break;

        case BEHAVIOR_IDLE:
        default:
            *target_x = (int)(random_value % 13U) - 6;
            *target_y = (int)((random_value >> 8) % 7U) - 3;
            break;
    }
}

static uint32_t behavior_pause_ms(
    behavior_state_t behavior
)
{
    switch (behavior)
    {
        case BEHAVIOR_ALERT:
            return 220U + (esp_random() % 380U);

        case BEHAVIOR_HAPPY:
            return 500U + (esp_random() % 800U);

        case BEHAVIOR_CURIOUS:
            return 800U + (esp_random() % 1300U);

        case BEHAVIOR_THINKING:
            return 1300U + (esp_random() % 1700U);

        case BEHAVIOR_LISTENING:
            return 1600U + (esp_random() % 1800U);

        case BEHAVIOR_SLEEPY:
            return 1800U + (esp_random() % 2400U);

        case BEHAVIOR_IDLE:
        default:
            return 900U + (esp_random() % 1800U);
    }
}

static uint32_t behavior_blink_divisor(
    behavior_state_t behavior
)
{
    switch (behavior)
    {
        case BEHAVIOR_SLEEPY:
            return 2U;

        case BEHAVIOR_HAPPY:
            return 3U;

        case BEHAVIOR_ALERT:
            return 6U;

        case BEHAVIOR_THINKING:
            return 6U;

        case BEHAVIOR_LISTENING:
            return 7U;

        case BEHAVIOR_CURIOUS:
            return 5U;

        case BEHAVIOR_IDLE:
        default:
            return 4U;
    }
}


static QueueHandle_t face_command_queue;

static behavior_state_t command_to_behavior(
    face_command_t command
)
{
    switch (command)
    {
        case FACE_COMMAND_CURIOUS:
            return BEHAVIOR_CURIOUS;

        case FACE_COMMAND_THINKING:
            return BEHAVIOR_THINKING;

        case FACE_COMMAND_LISTENING:
            return BEHAVIOR_LISTENING;

        case FACE_COMMAND_HAPPY:
            return BEHAVIOR_HAPPY;

        case FACE_COMMAND_SLEEPY:
            return BEHAVIOR_SLEEPY;

        case FACE_COMMAND_ALERT:
            return BEHAVIOR_ALERT;

        case FACE_COMMAND_IDLE:
        default:
            return BEHAVIOR_IDLE;
    }
}

static face_expression_t command_expression(
    face_command_t command
)
{
    if (command == FACE_COMMAND_SPEAKING)
    {
        return FACE_SPEAKING;
    }

    return behavior_expression(
        command_to_behavior(command)
    );
}

/*
 * Thread-safe entry point for future button, microphone, UART and Wi-Fi code.
 * timeout_ms == 0 performs a non-blocking request.
 */
bool face_send_command(
    face_command_t command,
    uint32_t timeout_ms
)
{
    if (face_command_queue == NULL)
    {
        return false;
    }

    return xQueueSend(
        face_command_queue,
        &command,
        pdMS_TO_TICKS(timeout_ms)
    ) == pdTRUE;
}

static void run_commanded_face(
    face_command_t command,
    int *gaze_x,
    int *gaze_y
)
{
    face_expression_t expression =
        command_expression(command);

    behavior_state_t behavior =
        command_to_behavior(command);

    int openness =
        (command == FACE_COMMAND_SLEEPY) ? 55 : 100;

    if (
        command == FACE_COMMAND_LISTENING ||
        command == FACE_COMMAND_SPEAKING
    )
    {
        face_animate_gaze(
            *gaze_x,
            *gaze_y,
            0,
            0,
            3,
            expression
        );

        *gaze_x = 0;
        *gaze_y = 0;
    }

    face_draw(
        *gaze_x,
        *gaze_y,
        openness,
        expression
    );

    /*
     * Hold external states until another command arrives.
     * Small animation keeps the face alive while commanded.
     */
    while (true)
    {
        face_command_t next_command;

        if (
            xQueueReceive(
                face_command_queue,
                &next_command,
                pdMS_TO_TICKS(350)
            ) == pdTRUE
        )
        {
            if (next_command == FACE_COMMAND_AUTONOMOUS)
            {
                return;
            }

            command = next_command;
            expression = command_expression(command);
            behavior = command_to_behavior(command);
            openness =
                (command == FACE_COMMAND_SLEEPY) ? 55 : 100;

            if (
                command == FACE_COMMAND_LISTENING ||
                command == FACE_COMMAND_SPEAKING
            )
            {
                *gaze_x = 0;
                *gaze_y = 0;
            }

            face_draw(
                *gaze_x,
                *gaze_y,
                openness,
                expression
            );
        }
        else
        {
            if (command == FACE_COMMAND_SPEAKING)
            {
                int speaking_y =
                    ((esp_random() & 1U) != 0U) ? -1 : 1;

                face_draw(
                    *gaze_x,
                    speaking_y,
                    100,
                    FACE_SPEAKING
                );
            }
            else if (
                (esp_random() %
                 behavior_blink_divisor(behavior)) == 0U
            )
            {
                face_animate_blink(
                    *gaze_x,
                    *gaze_y,
                    expression
                );

                face_draw(
                    *gaze_x,
                    *gaze_y,
                    openness,
                    expression
                );
            }
        }
    }
}

static void face_behavior_task(void *parameter)
{
    (void)parameter;

    int gaze_x = 0;
    int gaze_y = 0;

    behavior_state_t behavior = BEHAVIOR_IDLE;
    face_expression_t expression = FACE_NEUTRAL;

    face_draw(gaze_x, gaze_y, 100, expression);

    while (true)
    {
        face_command_t command;

        if (
            xQueueReceive(
                face_command_queue,
                &command,
                0
            ) == pdTRUE
        )
        {
            if (command != FACE_COMMAND_AUTONOMOUS)
            {
                run_commanded_face(
                    command,
                    &gaze_x,
                    &gaze_y
                );
            }

            continue;
        }

        behavior = choose_next_behavior(behavior);
        expression = behavior_expression(behavior);

        uint32_t behavior_time_ms =
            behavior_duration_ms(behavior);

        TickType_t behavior_start = xTaskGetTickCount();

        while (
            ((xTaskGetTickCount() - behavior_start) *
             portTICK_PERIOD_MS) < behavior_time_ms
        )
        {
            if (
                xQueueReceive(
                    face_command_queue,
                    &command,
                    0
                ) == pdTRUE
            )
            {
                if (command != FACE_COMMAND_AUTONOMOUS)
                {
                    run_commanded_face(
                        command,
                        &gaze_x,
                        &gaze_y
                    );
                }

                break;
            }

            int target_x;
            int target_y;

            choose_behavior_gaze(
                behavior,
                &target_x,
                &target_y
            );

            face_animate_gaze(
                gaze_x,
                gaze_y,
                target_x,
                target_y,
                3,
                expression
            );

            gaze_x = target_x;
            gaze_y = target_y;

            int openness =
                (behavior == BEHAVIOR_SLEEPY) ? 55 : 100;

            face_draw(
                gaze_x,
                gaze_y,
                openness,
                expression
            );

            if (
                (esp_random() %
                 behavior_blink_divisor(behavior)) == 0U
            )
            {
                face_animate_blink(
                    gaze_x,
                    gaze_y,
                    expression
                );

                face_draw(
                    gaze_x,
                    gaze_y,
                    openness,
                    expression
                );
            }

            uint32_t pause_ms =
                behavior_pause_ms(behavior);

            /*
             * Split the pause so external commands are handled quickly.
             */
            while (pause_ms > 0U)
            {
                uint32_t slice_ms =
                    (pause_ms > 50U) ? 50U : pause_ms;

                if (
                    xQueueReceive(
                        face_command_queue,
                        &command,
                        pdMS_TO_TICKS(slice_ms)
                    ) == pdTRUE
                )
                {
                    if (command != FACE_COMMAND_AUTONOMOUS)
                    {
                        run_commanded_face(
                            command,
                            &gaze_x,
                            &gaze_y
                        );
                    }

                    pause_ms = 0U;
                    break;
                }

                pause_ms -= slice_ms;
            }
        }
    }
}

void behavior_initialize(void)
{
    face_command_queue = xQueueCreate(
        8,
        sizeof(face_command_t)
    );

    if (face_command_queue == NULL)
    {
        ESP_LOGE(TAG, "Face command queue creation failed");
        abort();
    }
}

void behavior_start(void)
{
    BaseType_t task_created = xTaskCreate(
        face_behavior_task,
        "face_behavior",
        6144,
        NULL,
        5,
        NULL
    );

    if (task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Face behavior task creation failed");
        abort();
    }
}
