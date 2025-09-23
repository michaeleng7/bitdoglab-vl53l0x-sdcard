#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "vl53l0x.h"
#include "lib_ssd1306\ssd1306.h"
#include "lib_ssd1306\ssd1306_fonts.h"
#include "lib\FatFs_SPI\ff15\source\ff.h"  // FatFs for SD

// === Definitions for pins and peripherals ===
#define PORT_I2C i2c0 // VL53L0X on I2C0 bus
#define PINO_SDA_I2C 0
#define PINO_SCL_I2C 1

#define LED_GREEN 11
#define LED_RED 13

#define SPI_PORT spi0 // SD CARD on SPI0 bus
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define INVALID_DISTANCE 2001 // Value to indicate invalid reading (>2m)
#define MAX_DISTANCE_CM 999 // Limit to display in cm, above that it displays in meters

FATFS fs;

// === Function to record distance on SD card ===
void record_distance(uint16_t distance_cm, const char* status, uint64_t time_ms) {
    FIL file;
    char line[80], value_str[16], unit[4];

    // Decide the unit and value to be recorded
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

    // Calculates time in minutes and seconds since boot
    unsigned long minutes = time_ms / 60000;
    unsigned long seconds = (time_ms / 1000) % 60;

    // Open file and record line
    FRESULT fr = f_open(&file, "distance.txt", FA_OPEN_APPEND | FA_WRITE);
    if (fr == FR_OK) {
        snprintf(line, sizeof(line), "[%02lu:%02lu] Distance: %s %s - Status: %s\n",
                 minutes, seconds, value_str, unit, status);
        UINT bytes_written;
        f_write(&file, line, strlen(line), &bytes_written);
        f_close(&file);
    } else {
        printf("Error opening file: %d\n", fr);
    }
}

// === SD Card Initialization ===
void initialize_sd() {
    spi_init(SPI_PORT, 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        printf("Error when mounting SD: %d\n", fr);
    } else {
        printf("SD card mounted successfully.\n");
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

    // Inicializa I2C antes do display e sensor
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

    // Initialize LEDs
    gpio_init(LED_GREEN); gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_init(LED_RED); gpio_set_dir(LED_RED, GPIO_OUT);

    // Initialize SD
    initialize_sd();

    //Initializes VL53L0X sensor
    vl53l0x_device sensor;
    printf("Starting VL53L0X...\n");
    if (!vl53l0x_boot(&sensor, PORT_I2C)) {
        printf("ERROR: Failed to initialize sensor VL53L0X.\n");
        while (1);
    }
    printf("VL53L0X sensor initialized successfully.\n");

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
        } else if (distance_cm > MAX_DISTANCE_CM) {
            printf("Out of reach.\n");
        } else {
            // Register to SD card only if position changed
            record_distance(distance_cm, port_status, time_ms);

            gpio_put(LED_GREEN, strcmp(port_status, "OPEN") == 0);
            gpio_put(LED_RED, strcmp(port_status, "OPEN") != 0);
        }
        sleep_ms(200);
    }
    return 0;
}