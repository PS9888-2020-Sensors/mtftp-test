#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "mtftp_server.hpp"
#include "mtftp_client.hpp"

#include <sdkconfig.h>

uint16_t LEN_TEST_FILE = 1000;

extern "C" {
    void app_main();
}

const char *TAG = "mtftp-test";

QueueHandle_t packet_queue;

enum packet_dst {
    DST_CLIENT,
    DST_SERVER
};

typedef struct {
    enum packet_dst dst;
    uint8_t *data;
    uint8_t len;
} packet_container_t;

bool readFile(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br) {
    ESP_LOGD(TAG, "readFile: file_index=%d file_offset=%d btr=%d", file_index, file_offset, btr);

    if (file_offset > LEN_TEST_FILE) {
        *br = 0;
        return true;
    } else if ((file_offset + btr) > LEN_TEST_FILE) {
        *br = LEN_TEST_FILE - file_offset;
    } else {
        *br = btr;
    }

    // fake "reading" some data
    memset(data, 0xA5, *br);
    return true;
}

bool writeFile(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btw) {
    ESP_LOGD(TAG, "writeFile: file_index=%d file_offset=%d btw=%d", file_index, file_offset, btw);
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, btw, ESP_LOG_DEBUG);

    return true;
}

void sendPacket(enum packet_dst dst, uint8_t *data, uint8_t len) {
    uint8_t *buf = (uint8_t *) malloc(len);

    if (buf == NULL) {
        ESP_LOGW(TAG, "sendPacket: malloc for buf failed");
        return;
    }

    memcpy(buf, data, len);

    packet_container_t *container = (packet_container_t *) malloc(sizeof(packet_container_t));

    if (container == NULL) {
        ESP_LOGW(TAG, "sendPacket: malloc for container failed");

        free(buf);
        return;
    }

    container->dst = dst;
    container->data = buf;
    container->len = len;

    xQueueSend(packet_queue, container, 0);

    // container is queued by copy, so isnt needed anymore
    free(container);
}

void sendPacketToServer(uint8_t *data, uint8_t len) {
    sendPacket(DST_SERVER, data, len);
}

void sendPacketToClient(uint8_t *data, uint8_t len) {
    sendPacket(DST_CLIENT, data, len);
}

void app_main(void)
{
    packet_container_t container;
    packet_queue = xQueueCreate(8, sizeof(packet_container_t));

    MtftpServer server;
    server.init(&readFile, &sendPacketToClient);

    MtftpClient client;
    client.init(&writeFile, &sendPacketToServer);

    client.beginRead(0, 0);

    while (1) {
        if (xQueueReceive(packet_queue, &container, 0) == pdTRUE) {
            if (container.dst == DST_SERVER) {
                server.onPacketRecv(container.data, container.len);
            } else {
                client.onPacketRecv(container.data, container.len);
            }

            free(container.data);
        }

        server.loop();

        vTaskDelay(1);
    }
}
