#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include <math.h>

#include "driver/gpio.h"
#include <driver/uart.h>
#include <driver/ledc.h>

#include "freertos/FreeRTOS.h"

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <homekit/types.h>

#include "wifi.h"

#define MOTOR_PWM_CHANNEL_RED LEDC_CHANNEL_0
#define MOTOR_PWM_CHANNEL_GREEN LEDC_CHANNEL_1
#define MOTOR_PWM_CHANNEL_BLUE LEDC_CHANNEL_2
#define MOTOR_PWM_TIMER LEDC_TIMER_1
#define MOTOR_PWM_BIT_NUM LEDC_TIMER_10_BIT

#define RED_PWM_PIN GPIO_NUM_4
#define GREEN_PWM_PIN GPIO_NUM_16
#define BLUE_PWM_PIN GPIO_NUM_17
#define LED_RGB_SCALE 255       // this is the scaling factor used for color conversion

// Global variables
float led_hue = 0;              // hue is scaled 0 to 360
float led_saturation = 59;      // saturation is scaled 0 to 100
float led_brightness = 100;     // brightness is scaled 0 to 100
bool led_on = false;            // on is boolean on or off

typedef struct rgb_color
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
} RGB;


void on_wifi_ready();

//http://blog.saikoled.com/post/44677718712/how-to-convert-from-hsi-to-rgb-white
static void hsi2rgb(float h, float s, float i, RGB* rgb) {
    int r, g, b;

    while (h < 0) { h += 360.0F; };     // cycle h around to 0-360 degrees
    while (h >= 360) { h -= 360.0F; };
    h = 3.14159F*h / 180.0F;            // convert to radians.
    s /= 100.0F;                        // from percentage to ratio
    i /= 100.0F;                        // from percentage to ratio
    s = s > 0 ? (s < 1 ? s : 1) : 0;    // clamp s and i to interval [0,1]
    i = i > 0 ? (i < 1 ? i : 1) : 0;    // clamp s and i to interval [0,1]
    i = i * sqrt(i);                    // shape intensity to have finer granularity near 0

    if (h < 2.09439) {
        r = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        g = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        b = LED_RGB_SCALE * i / 3 * (1 - s);
    }
    else if (h < 4.188787) {
        h = h - 2.09439;
        g = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        b = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        r = LED_RGB_SCALE * i / 3 * (1 - s);
    }
    else {
        h = h - 4.188787;
        b = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        r = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        g = LED_RGB_SCALE * i / 3 * (1 - s);
    }

    rgb->red = (uint8_t) r;
    rgb->green = (uint8_t) g;
    rgb->blue = (uint8_t) b;
    rgb->white = (uint8_t) 0;           // white channel is not used
}

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            printf("STA start\n");
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            printf("WiFI ready\n");
            on_wifi_ready();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            printf("STA disconnected\n");
            esp_wifi_connect();
            break;
        default:
            break;
    }
    return ESP_OK;
}

void wifi_init() {

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t sta_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
        
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

}

void motor_pwm_init(void)
{
    ledc_channel_config_t ledc_channel_red = {}, ledc_channel_green = {0}, ledc_channel_blue = {0};

    ledc_channel_red.gpio_num = RED_PWM_PIN;
    ledc_channel_red.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel_red.channel = MOTOR_PWM_CHANNEL_RED;
    ledc_channel_red.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_red.timer_sel = MOTOR_PWM_TIMER;
    ledc_channel_red.duty = 0;

    ledc_channel_green.gpio_num = GREEN_PWM_PIN;
    ledc_channel_green.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel_green.channel = MOTOR_PWM_CHANNEL_GREEN;
    ledc_channel_green.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_green.timer_sel = MOTOR_PWM_TIMER;
    ledc_channel_green.duty = 0;
	
    ledc_channel_blue.gpio_num = BLUE_PWM_PIN;
    ledc_channel_blue.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel_blue.channel = MOTOR_PWM_CHANNEL_BLUE;
    ledc_channel_blue.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_blue.timer_sel = MOTOR_PWM_TIMER;
    ledc_channel_blue.duty = 0;
	
    ledc_timer_config_t ledc_timer = {0};
    ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_timer.bit_num = MOTOR_PWM_BIT_NUM;
    ledc_timer.timer_num = MOTOR_PWM_TIMER;
    ledc_timer.freq_hz = 25000;
	
	ESP_ERROR_CHECK( ledc_channel_config(&ledc_channel_red) );
	ESP_ERROR_CHECK( ledc_channel_config(&ledc_channel_green) );
	ESP_ERROR_CHECK( ledc_channel_config(&ledc_channel_blue) );
	ESP_ERROR_CHECK( ledc_timer_config(&ledc_timer) );
}

void motor_pwm_set(float duty_fraction, ledc_channel_t channel) {
	uint32_t max_duty = (1 << MOTOR_PWM_BIT_NUM) - 1;
	uint32_t duty_cycle = (uint32_t)(duty_fraction * (float)max_duty);

    printf("DutyCycle: %d", duty_cycle);
	
	ESP_ERROR_CHECK( ledc_set_duty(LEDC_HIGH_SPEED_MODE, channel, duty_cycle) );
	ESP_ERROR_CHECK( ledc_update_duty(LEDC_HIGH_SPEED_MODE, channel) );
}

void updateLED() {

    printf("\n\nDados:\nON: %d\nHue: %.2f\nBrightness: %.2f\nSaturation: %.2f\n\n", led_on, led_hue, led_brightness, led_saturation);

    RGB rgb = {};

    if (led_on){
        hsi2rgb(led_hue, led_saturation, led_brightness, &rgb);

        printf("R: %d, G: %d, B: %d\n\n", rgb.red, rgb.green, rgb.blue);

        motor_pwm_set(1.0 - (1.0 * rgb.red / LED_RGB_SCALE), MOTOR_PWM_CHANNEL_RED);
        motor_pwm_set(1.0 - (1.0 * rgb.green / LED_RGB_SCALE), MOTOR_PWM_CHANNEL_GREEN);
        motor_pwm_set(1.0 - (1.0 * rgb.blue / LED_RGB_SCALE), MOTOR_PWM_CHANNEL_BLUE);
    } else {
        motor_pwm_set(1.0, MOTOR_PWM_CHANNEL_RED);
        motor_pwm_set(1.0, MOTOR_PWM_CHANNEL_GREEN);
        motor_pwm_set(1.0, MOTOR_PWM_CHANNEL_BLUE);
    }
}

void led_identify(homekit_value_t _value) {
    printf("LED identify\n");
}

homekit_value_t get_floodlight_on() {
    return HOMEKIT_BOOL(led_on);
}

void set_floodlight_on(homekit_value_t value) {
    if (value.format != homekit_format_bool) {
        printf("Invalid on-value format: %d\n", value.format);
        return;
    }

    led_on = value.bool_value;
    updateLED();
}

homekit_value_t get_floodlight_brightness() {
    return HOMEKIT_INT(led_brightness);
}

void set_floodlight_brightness(homekit_value_t value) {
    if (value.format != homekit_format_int) {
        printf("Invalid brightness-value format: %d\n", value.format);
        return;
    }
    led_brightness = value.int_value;
    updateLED();
}

homekit_value_t get_floodlight_hue() {
    return HOMEKIT_FLOAT(led_hue);
}

void set_floodlight_hue(homekit_value_t value) {
    if (value.format != homekit_format_float) {
        printf("Invalid hue-value format: %d\n", value.format);
        return;
    }
    led_hue = value.float_value;
    updateLED();
}

homekit_value_t get_floodlight_saturation() {
    return HOMEKIT_FLOAT(led_saturation);
}

void set_floodlight_saturation(homekit_value_t value) {
        if (value.format != homekit_format_float) {
        printf("Invalid sat-value format: %d\n", value.format);
        return;
    }
    led_saturation = value.float_value;
    updateLED();
}

homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_switch, .services=(homekit_service_t*[]){
            HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                HOMEKIT_CHARACTERISTIC(NAME, "LED 01"),
                HOMEKIT_CHARACTERISTIC(MANUFACTURER, "Veevaz"),
                HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "LED01"),
                HOMEKIT_CHARACTERISTIC(MODEL, "LED"),
                HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.1"),
                HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
                NULL
            }),
            HOMEKIT_SERVICE(LIGHTBULB, .primary=true, .characteristics=(homekit_characteristic_t*[]){
                HOMEKIT_CHARACTERISTIC(NAME, "Lâmpada"),
                HOMEKIT_CHARACTERISTIC(
                    ON, false,
                    .getter=get_floodlight_on,
                    .setter=set_floodlight_on
                ),
                HOMEKIT_CHARACTERISTIC(
                    BRIGHTNESS, false,
                    .getter=get_floodlight_brightness,
                    .setter=set_floodlight_brightness
                ),
                HOMEKIT_CHARACTERISTIC(
                    HUE, false,
                    .getter=get_floodlight_hue,
                    .setter=set_floodlight_hue
                ),
                HOMEKIT_CHARACTERISTIC(
                    SATURATION, false,
                    .getter=get_floodlight_saturation,
                    .setter=set_floodlight_saturation
                ),
                NULL
            }),
            NULL
        }),
        NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111",
    .setupId = "9B81",
};

void on_wifi_ready() {
    homekit_server_init(&config);
}

void app_main(void)
{
    uart_set_baudrate(0, 115200);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    motor_pwm_init();

    // gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    // gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    // int level = 0;
    // while (true) {
    //     gpio_set_level(GPIO_NUM_2, level);
    //     level = !level;
    //     vTaskDelay(300 / portTICK_PERIOD_MS);
    // }
}

