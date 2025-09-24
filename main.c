#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "vl53l0x.h"
#include "lib_ssd1306\ssd1306.h"
#include "lib_ssd1306\ssd1306_fonts.h"
#include "lib\FatFs_SPI\ff15\source\ff.h"  // FatFs for SD

// === Definitions for pins and peripherals ===
#define PORT_I2C i2c0 // VL53L0X on I2C0 bus
#define PINO_SDA_I2C 0
#define PINO_SCL_I2C 1

#define BUZZER_PIN 21    // BitDogLab internal buzzer pin
#define BUZZER_DISTANCE_THRESHOLD 10  // Distance threshold in cm to trigger buzzer
#define BUZZER_FREQ 4000  // Frequency in Hz for the buzzer

#define LED_GREEN 11
#define LED_RED 13

#define SPI_PORT spi0 // SD CARD on SPI0 bus
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define INVALID_DISTANCE 2001 // Value to indicate invalid reading (>2m)
#define MAX_DISTANCE_CM 999 // Limit to display in cm, above that it displays in meters
#define DISTANCE_OFFSET_MM 30  // Calibration offset in millimeters

FATFS fs;

// === Function to record distance on SD card ===
void record_distance(uint16_t distance_cm, const char* status, uint64_t time_ms) {
    static bool first_write = true;
    FIL file;
    FRESULT fr;

    // If it is the first time it is written, it creates a header.
    if (first_write) {
        fr = f_open(&file, "distance.txt", FA_WRITE | FA_CREATE_ALWAYS);
        if (fr == FR_OK) {
            f_printf(&file, "Time,Distance,Unit,Status\n");
            f_close(&file);
            first_write = false;
        }
    }

    fr = f_open(&file, "distance.txt", FA_OPEN_APPEND | FA_WRITE);
    if (fr == FR_OK) {
        char line[80];
        unsigned long minutes = time_ms / 60000;
        unsigned long seconds = (time_ms / 1000) % 60;

        if (distance_cm >= 100 && distance_cm < INVALID_DISTANCE) {
            f_printf(&file, "%02lu:%02lu,%.2f,m,%s\n", 
                    minutes, seconds, distance_cm / 100.0f, status);
        } else if (distance_cm == INVALID_DISTANCE) {
            f_printf(&file, "%02lu:%02lu,ERROR,-,%s\n", 
                    minutes, seconds, status);
        } else {
            f_printf(&file, "%02lu:%02lu,%d,cm,%s\n", 
                    minutes, seconds, distance_cm, status);
        }
        
        f_close(&file);
    } else {
        printf("File open failed: %d\n", fr);
    }
}

// === SD Card Initialization ===
void initialize_sd() {
    // Reduces SPI speed for increased reliability
    spi_init(SPI_PORT, 400 * 1000); // Reduces to 400kHz

    // Configura os pinos SPI
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    
    // Configura o CS com pull-up
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_pull_up(PIN_CS);

    // Short delay for stabilization
    sleep_ms(100);

    printf("Initializing SD card...\n");
    FRESULT fr = f_mount(&fs, "", 1);
    
    if (fr == FR_NO_FILESYSTEM) {
        printf("No filesystem found. Formatting card...\n");
        MKFS_PARM opt = {FM_FAT32, 0, 0, 0, 0};
        BYTE work[FF_MAX_SS];
        fr = f_mkfs("", &opt, work, sizeof(work));
        if (fr == FR_OK) {
            printf("Format successful. Mounting...\n");
            fr = f_mount(NULL, "", 0);      // Unmount first
            fr = f_mount(&fs, "", 1);       // Mount again
        }
    }

    if (fr != FR_OK) {
        printf("SD card mount failed (%d)\n", fr);
        return;
    }

    // Try to create/open test file
    FIL fil;
    fr = f_open(&fil, "test.txt", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        f_close(&fil);
        printf("SD card and filesystem OK\n");
    } else {
        printf("File creation failed (%d)\n", fr);
    }
}

// === Displays information on the OLED screen ===
void display_oled(uint16_t distance_cm, const char* port_status) {
    char buffer[32];
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("DISTANCE MONITOR", Font_6x8, White);

    // Shows distance in meters, cm or error
    if (distance_cm >= 100 && distance_cm < INVALID_DISTANCE) {
        snprintf(buffer, sizeof(buffer), "DISTANCE: %.2f m", distance_cm / 100.0f);
    } else if (distance_cm == INVALID_DISTANCE) {
        snprintf(buffer, sizeof(buffer), "SENSOR ERROR");
    } else {
        snprintf(buffer, sizeof(buffer), "DISTANCE: %d cm", distance_cm);
    }

    ssd1306_SetCursor(0, 16);
    ssd1306_WriteString(buffer, Font_6x8, White);

    snprintf(buffer, sizeof(buffer), "AUT-ACCESS: %s", port_status);
    ssd1306_SetCursor(0, 32);
    ssd1306_WriteString(buffer, Font_6x8, White);

    ssd1306_UpdateScreen();
}

// === Main function ===
int main() {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);

    // Initialize I2C before display and sensor
    i2c_init(PORT_I2C, 100 * 1000);
    gpio_set_function(PINO_SDA_I2C, GPIO_FUNC_I2C);
    gpio_set_function(PINO_SCL_I2C, GPIO_FUNC_I2C);
    gpio_pull_up(PINO_SDA_I2C);
    gpio_pull_up(PINO_SCL_I2C);

    // Initializes OLED display
    printf("Starting SSD1306...\n");
    ssd1306_Init();
    ssd1306_Fill(Black);
    ssd1306_UpdateScreen();
    printf("Display SSD1306 OK\n");

    // Initialize SD
    initialize_sd();
    sleep_ms(1000); // Give the SD time to stabilize
    
    // Initialize LEDs
    gpio_init(LED_GREEN); gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_init(LED_RED); gpio_set_dir(LED_RED, GPIO_OUT);

    // Initializes VL53L0X sensor
    vl53l0x_device sensor;
    printf("Starting VL53L0X...\n");
    sensor.time_timeout = 5000; // Increase timeout to 5 seconds
    if (!vl53l0x_boot(&sensor, PORT_I2C)) {
        printf("ERROR: Failed to initialize sensor VL53L0X.\n");
        while (1);
    }
    printf("VL53L0X sensor initialized successfully.\n");

    // Initialize Buzzer with PWM
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint channel = pwm_gpio_to_channel(BUZZER_PIN);
    
    // Set frequency (4kHz)
    uint32_t clock = 125000000;
    uint32_t divider16 = clock / BUZZER_FREQ / 4096 + (clock % (BUZZER_FREQ * 4096) != 0);
    if (divider16 / 16 == 0)
        divider16 = 16;
    uint32_t wrap = clock * 16 / divider16 / BUZZER_FREQ - 1;
    pwm_set_clkdiv_int_frac(slice_num, divider16/16, divider16 & 0xF);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_enabled(slice_num, true);

    vl53l0x_start_continuous(&sensor, 0);
    printf("Sensor in continuous mode. Collecting data...\n");

    uint8_t ultima_posicao = 255;

    // === Main loop ===
    while (1) {
        // Reads distance from sensor
        uint16_t distance_cm = vl53l0x_reads_distance_from_sensor_cm(&sensor);
        uint64_t time_ms = to_ms_since_boot(get_absolute_time());

        char value_str[16], unit[4];
        const char* port_status = "CLOSE";

        // Decide unit for terminal and logic
        if (distance_cm >= 100 && distance_cm < INVALID_DISTANCE) {
            snprintf(value_str, sizeof(value_str), "%.2f", distance_cm / 100.0f);
            strcpy(unit, "m");
        } else if (distance_cm == INVALID_DISTANCE) {
            strcpy(value_str, "ERROR");
            strcpy(unit, "");
        } else {
            snprintf(value_str, sizeof(value_str), "%d", distance_cm);
            strcpy(unit, "cm");
        }

        // Sets port state
        if (distance_cm < 10) {
            port_status = "OPEN";
        }

        // Displays status and distance on the terminal
        printf("Status: %s | Distance: %s %s\n", port_status, value_str, unit);

        // Updates OLED display
        display_oled(distance_cm, port_status);

        // Error handling and out-of-range logic
        if (distance_cm == INVALID_DISTANCE) {
            printf("Reading error.\n");
            gpio_put(BUZZER_PIN, 0);
            // Turns off both LEDs on error
            gpio_put(LED_GREEN, 0);
            gpio_put(LED_RED, 0);
        } else if (distance_cm > MAX_DISTANCE_CM) {
            printf("Out of reach.\n");
            gpio_put(BUZZER_PIN, 0);
            // Turns off both LEDs when out of range
            gpio_put(LED_GREEN, 0);
            gpio_put(LED_RED, 0);
        } else {
            // Register to SD card
            record_distance(distance_cm, port_status, time_ms);

            // LED logic
            if (distance_cm < 10) {  // Very close - Red alert
                gpio_put(LED_GREEN, 0);
                gpio_put(LED_RED, 1);
            } else if (distance_cm < 50) {  // Object detected - green LED
                gpio_put(LED_GREEN, 1);
                gpio_put(LED_RED, 0);
            } else {  // No objects nearby - LEDs off
                gpio_put(LED_GREEN, 0);
                gpio_put(LED_RED, 0);
            }

            // Buzzer control with soft beep pattern
            static uint32_t last_buzzer_toggle = 0;
            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            
            if (distance_cm < BUZZER_DISTANCE_THRESHOLD) {
                uint32_t elapsed_time = current_time - last_buzzer_toggle;
                
                if (elapsed_time >= 1100) { // Reset cycle after 1.1 seconds
                    last_buzzer_toggle = current_time;
                    // Set 50% duty cycle for clear beep
                    pwm_set_chan_level(slice_num, channel, wrap / 2);
                } else if (elapsed_time >= 100) { // Turn off after 100ms
                    // Set 0% duty cycle to turn off
                    pwm_set_chan_level(slice_num, channel, 0);
                }
            } else {
                pwm_set_chan_level(slice_num, channel, 0);
                last_buzzer_toggle = current_time;
            }
        }
        
        sleep_ms(200);
    }
    return 0;
}