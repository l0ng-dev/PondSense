#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t last_error;
    uint32_t refresh_count;
    uint32_t last_refresh_ms;
    bool initialized;
} app_iwdt_status_t;

extern app_iwdt_status_t g_app_iwdt_status;

void App_Runtime_Run(void);

#endif
