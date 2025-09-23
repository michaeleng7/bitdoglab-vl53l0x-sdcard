
#include "vl53l0x.h"
#include "pico/stdlib.h"
#include <string.h>

// Standard measurement time in microseconds
#define STANDARD_TIME_MEASUREMENT 33000
//Value representing an invalid distance (as the sensor can measure up to 2 meters)
#define INVALID_DISTANCE 2001

// ========================== Auxiliary functions ==========================

// Writes an 8-bit value to a sensor register
static void write_reg(vl53l0x_device* dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(dev->i2c, dev->address, buf, 2, false);
}

// Writes a 16-bit value to a sensor register
static void write_reg16(vl53l0x_device* dev, uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (val >> 8), (val & 0xFF)};
    i2c_write_blocking(dev->i2c, dev->address, buf, 3, false);
}

// Reads an 8-bit value from a sensor register
static uint8_t read_reg(vl53l0x_device* dev, uint8_t reg) {
    uint8_t val;
    i2c_write_blocking(dev->i2c, dev->address, &reg, 1, true);
    i2c_read_blocking(dev->i2c, dev->address, &val, 1, false);
    return val;
}

// Reads a 16-bit value from a sensor register
static uint16_t read_reg16(vl53l0x_device* dev, uint8_t reg) {
    uint8_t buf[2];
    i2c_write_blocking(dev->i2c, dev->address, &reg, 1, true);
    i2c_read_blocking(dev->i2c, dev->address, buf, 2, false);
    return ((uint16_t)buf[0] << 8) | buf[1];
}

// Returns the current time in milliseconds since boot
static inline uint32_t current_time_ms() {
    return to_ms_since_boot(get_absolute_time());
}

// ========================== Sensor initialization ==========================

bool vl53l0x_boot(vl53l0x_device* dev, i2c_inst_t* port_i2c) {
    // Configures the I2C instance and sensor address
    dev->i2c = port_i2c;
    dev->address = ADDRESS_VL53L0X;
    dev->time_timeout = 1000; // 1 second timeout

    // VL53L0X Boot Sequence (Internal Configuration)
    write_reg(dev, 0x80, 0x01);
    write_reg(dev, 0xFF, 0x01);
    write_reg(dev, 0x00, 0x00);
    dev->stop_variable = read_reg(dev, 0x91); // Stores stop value
    write_reg(dev, 0x00, 0x01);
    write_reg(dev, 0xFF, 0x00);
    write_reg(dev, 0x80, 0x00);

    // Configure measurement parameters
    write_reg(dev, 0x60, read_reg(dev, 0x60) | 0x12);
    write_reg16(dev, 0x44, (uint16_t)(0.25 * (1 << 7))); // Ajuste de ganho
    write_reg(dev, 0x01, 0xFF);

    // Additional configuration for startup
    write_reg(dev, 0x80, 0x01);
    write_reg(dev, 0xFF, 0x01);
    write_reg(dev, 0x00, 0x00);
    write_reg(dev, 0xFF, 0x06);
    write_reg(dev, 0x83, read_reg(dev, 0x83) | 0x04);
    write_reg(dev, 0xFF, 0x07);
    write_reg(dev, 0x81, 0x01);
    write_reg(dev, 0x80, 0x01);
    write_reg(dev, 0x94, 0x6B);
    write_reg(dev, 0x83, 0x00);

    // Waiting for sensor response with timeout
    uint32_t start = current_time_ms();
    while (read_reg(dev, 0x83) == 0x00) {
        if (current_time_ms() - start > dev->time_timeout) return false;
    }

    // Ends boot sequence
    write_reg(dev, 0x83, 0x01);
    read_reg(dev, 0x92);
    write_reg(dev, 0x81, 0x00);
    write_reg(dev, 0xFF, 0x06);
    write_reg(dev, 0x83, read_reg(dev, 0x83) & ~0x04);
    write_reg(dev, 0xFF, 0x01);
    write_reg(dev, 0x00, 0x01);
    write_reg(dev, 0xFF, 0x00);
    write_reg(dev, 0x80, 0x00);

    // Sets default measurement mode
    write_reg(dev, 0x0A, 0x04);
    write_reg(dev, 0x84, read_reg(dev, 0x84) & ~0x10);
    write_reg(dev, 0x0B, 0x01);

    // Sets measurement time and starts the sensor
    dev->measurement_time = STANDARD_TIME_MEASUREMENT;
    write_reg(dev, 0x01, 0xE8);
    write_reg16(dev, 0x04, STANDARD_TIME_MEASUREMENT / 1085);

    write_reg(dev, 0x0B, 0x01);
    return true;
}

// ========================== Continuous mode ==========================

void vl53l0x_start_continuous(vl53l0x_device* dev, uint32_t period_ms) {
    // Resets registers to continuous mode
    write_reg(dev, 0x80, 0x01);
    write_reg(dev, 0xFF, 0x01);
    write_reg(dev, 0x00, 0x00);
    write_reg(dev, 0x91, dev->stop_variable);
    write_reg(dev, 0x00, 0x01);
    write_reg(dev, 0xFF, 0x00);
    write_reg(dev, 0x80, 0x00);

    // Sets the continuous measurement period
    if (period_ms != 0) {
        write_reg16(dev, 0x04, period_ms * 12 / 13);
        write_reg(dev, 0x00, 0x04); // Continuous mode with interval
    } else {
        write_reg(dev, 0x00, 0x02); // Continuous mode without gap
    }
}

// ========================== Continuous reading ==========================

uint16_t vl53l0x_reads_distance_from_sensor_cm(vl53l0x_device* dev) {
    // Waiting for new measurement with timeout
    uint32_t start = current_time_ms();
    while ((read_reg(dev, 0x13) & 0x07) == 0) {
        if (current_time_ms() - start > dev->time_timeout) return INVALID_DISTANCE;
    }

    // Reads distance in millimeters
    uint16_t distance_mm = read_reg16(dev, 0x1E);
    write_reg(dev, 0x0B, 0x01); // Clear interrupt flag

    // Check if the distance is valid
    if (distance_mm >= 2001 || distance_mm == INVALID_DISTANCE) return INVALID_DISTANCE;

    // Convert to centimeters
    return distance_mm / 10;
}