// main/logging.c
#include "tasks.h"
#include "config.h"

#if LOGGING_ENABLED

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct {
    char      message[MAX_LOG_MSG_LEN];
    TickType_t timestamp;
} LogMessage;

static QueueHandle_t LOG_MESSAGE_QUEUE = NULL;

static void logger_task(void *arg)
{
    (void)arg;
    LogMessage msg;

    while (true) {
        if (xQueueReceive(LOG_MESSAGE_QUEUE, &msg, portMAX_DELAY) == pdTRUE) {
            printf("[%lu] %s\n", (unsigned long)msg.timestamp, msg.message);
            fflush(stdout);
        }
    }
}

void fast_log(const char *fmt, ...)
{
    if (LOG_MESSAGE_QUEUE == NULL) return;

    LogMessage msg;
    msg.timestamp = xTaskGetTickCount();

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg.message, sizeof(msg.message), fmt, ap);
    va_end(ap);

    // If full, drop the oldest by overwriting the front
    xQueueSend(LOG_MESSAGE_QUEUE, &msg, 0);
}

void logger_init(void)
{
    LOG_MESSAGE_QUEUE = xQueueCreate(LOG_MESSAGE_QUEUE_LENGTH,
                                     sizeof(LogMessage));
    if (!LOG_MESSAGE_QUEUE) {
        printf("LOGGER (F): cannot create queue\n");
        fflush(stdout);
        vTaskDelay(portMAX_DELAY);
    }

    xTaskCreate(logger_task,
                LOGGER_TASK_NAME,
                LOGGER_MEM,
                NULL,
                LOGGER_TASK_PRIORITY,
                NULL);
}

#else   // LOGGING_ENABLED == 0

void logger_init(void) {}
void fast_log(const char *fmt, ...) { (void)fmt; }

#endif
