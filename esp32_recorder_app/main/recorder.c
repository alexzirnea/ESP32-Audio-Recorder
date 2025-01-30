#include "recorder.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "audio_sys.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined (CONFIG_CHOICE_WAV_ENCODER)
#include "wav_encoder.h"
#elif defined (CONFIG_CHOICE_OPUS_ENCODER)
#include "opus_encoder.h"
#elif defined (CONFIG_CHOICE_AAC_ENCODER)
#include "aac_encoder.h"
#if defined (CONFIG_ESP_LYRAT_MINI_V1_1_BOARD)
#include "filter_resample.h"
#endif
#elif defined (CONFIG_CHOICE_AMR_WB_ENCODER)
#include "amrwb_encoder.h"
#elif defined (CONFIG_CHOICE_AMR_NB_ENCODER)
#include "amrnb_encoder.h"
#endif

#include "audio_idf_version.h"
#include "driver/gpio.h"


#define RECORD_TIME_SECONDS (10)
static const char *TAG = "RECORDER";

#if defined (CONFIG_CHOICE_OPUS_ENCODER)
#define SAMPLE_RATE         16000
#define CHANNEL             1
#define BIT_RATE            64000
#define COMPLEXITY          10
#endif

#if defined (CONFIG_CHOICE_AAC_ENCODER)
#define SAMPLE_RATE         16000
#define CHANNEL             2
#define BIT_RATE            80000
#endif

static void init()
{

}

void rec_mainTask(void * pv)
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t fatfs_stream_writer, i2s_stream_reader, audio_encoder;
    int channel_format = I2S_CHANNEL_TYPE_ONLY_LEFT;
    int sample_rate = 48000;

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "[ 1 ] Mount sdcard");
    //vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    ESP_LOGI(TAG, "[ 1 ] Mount sdcard");
    // Initialize SD Card peripheral
    gpio_pullup_en(ESP_SD_PIN_D0);
    gpio_pullup_en(ESP_SD_PIN_D1);
    gpio_pullup_en(ESP_SD_PIN_D2);
    gpio_pullup_en(ESP_SD_PIN_D3);
    audio_board_sdcard_init(set, SD_MODE_4_LINE);
    gpio_pullup_en(ESP_SD_PIN_D0);
    gpio_pullup_en(ESP_SD_PIN_D1);
    gpio_pullup_en(ESP_SD_PIN_D2);
    gpio_pullup_en(ESP_SD_PIN_D3);
    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    //audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_START);
    //vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "[3.0] Create audio pipeline for recording");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[3.1] Create fatfs stream to write data to sdcard");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_WRITER;
    fatfs_stream_writer = fatfs_stream_init(&fatfs_cfg);

    ESP_LOGI(TAG, "[3.2] Create i2s stream to read audio data from codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;

/*#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
    i2s_cfg.chan_cfg.id = 1;
#else
    i2s_cfg.i2s_port = 1;
#endif
*/
    i2s_cfg.chan_cfg.id = 0;
    i2s_stream_set_channel_type(&i2s_cfg, channel_format);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = sample_rate;
#else
    i2s_cfg.i2s_config.sample_rate = sample_rate;
#endif
    //vTaskDelay(5000 / portTICK_PERIOD_MS);
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[3.3] Create audio encoder to handle data");
    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    audio_encoder = wav_encoder_init(&wav_cfg);


    ESP_LOGI(TAG, "[3.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");

    audio_pipeline_register(pipeline, audio_encoder, "wav");

    audio_pipeline_register(pipeline, fatfs_stream_writer, "file");

    ESP_LOGI(TAG, "[3.5] Link it together [codec_chip]-->i2s_stream-->audio_encoder-->fatfs_stream-->[sdcard]");

    const char *link_tag[3] = {"i2s", "wav", "file"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[3.6] Set music info to fatfs");
    audio_element_info_t music_info = {0};
    audio_element_getinfo(i2s_stream_reader, &music_info);
    ESP_LOGI(TAG, "[ * ] Save the recording info to the fatfs stream writer, sample_rates=%d, bits=%d, ch=%d",
                music_info.sample_rates, music_info.bits, music_info.channels);
    audio_element_setinfo(fatfs_stream_writer, &music_info);

    ESP_LOGI(TAG, "[3.7] Set up  uri");

    audio_element_set_uri(fatfs_stream_writer, "/sdcard/rec.wav");

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    ESP_LOGI(TAG, "[ 6 ] Listen for all pipeline events, record for %d Seconds", RECORD_TIME_SECONDS);
    int second_recorded = 0;
    while (1) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, 1000 / portTICK_RATE_MS) != ESP_OK) {
            second_recorded ++;
            ESP_LOGI(TAG, "[ * ] Recording ... %d", second_recorded);
            if (second_recorded >= RECORD_TIME_SECONDS) {
                audio_element_set_ringbuf_done(i2s_stream_reader);
            }
            continue;
        }
        /* Stop when the last pipeline element (fatfs_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) fatfs_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED)
                || ((int)msg.data == AEL_STATUS_ERROR_OPEN))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "[ 7 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, audio_encoder);
    audio_pipeline_unregister(pipeline, i2s_stream_reader);
    audio_pipeline_unregister(pipeline, fatfs_stream_writer);


    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(fatfs_stream_writer);
    audio_element_deinit(i2s_stream_reader);

    audio_element_deinit(audio_encoder);
    esp_periph_set_destroy(set);
    vTaskDelete(NULL);
}