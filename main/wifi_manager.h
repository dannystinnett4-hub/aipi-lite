#pragma once

#include <stdbool.h>

typedef void (*wifi_connected_callback_t)(void);

bool wifi_manager_initialize(
    wifi_connected_callback_t connected_callback
);
