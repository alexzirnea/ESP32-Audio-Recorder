/* Record file to SD Card

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "recorder.h"
#include "driver/gpio.h"

void web_interface_task(void * pv);

#define ESP_INTR_FLAG_DEFAULT 0
#define GPIO_START_STOP_BUTTON 18
static const char *TAG = "MAIN";
static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            ESP_LOGI(TAG, "GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
        }
    }
}

void app_main(void)
{
    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    //bit mask of the pins
    io_conf.pin_bit_mask = 1ULL << GPIO_START_STOP_BUTTON;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    
    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreatePinnedToCore(web_interface_task, "web_interface_task", 32768, NULL, 5, NULL, 1);

    gpio_config(&io_conf);
    ESP_LOGI(TAG, "DONE GPIO CONFIG\n");
    //start gpio task
    xTaskCreatePinnedToCore(gpio_task_example, "gpio_task_example", 2048, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "DONE GPIO TASK CREATE\n");
    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_START_STOP_BUTTON, gpio_isr_handler, (void*) GPIO_START_STOP_BUTTON);
    ESP_LOGI(TAG, "DONE ISR\n");

    xTaskCreatePinnedToCore(rec_mainTask, "recorder_task", 4096, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "DONE REC TASK CREATE\n");
    while(1)
    {
        vTaskDelay(portMAX_DELAY);
    }
}
