
#ifndef VL53L0X_H
#define VL53L0X_H

// Inclusion of standard libraries for boolean and fixed-length integer types
#include <stdbool.h>     // Allows the use of the bool type (true/false)
#include <stdint.h>      // Allows the use of types like uint8_t, uint16_t, etc.

// Inclusion of the Raspberry Pi Pico-specific I2C communication library
#include "hardware/i2c.h"

// Sets the default I2C address of the VL53L0X sensor
#define ADDRESS_VL53L0X 0x29 // VL53L0X default hexadecimal address

// Structure representing a VL53L0X device
typedef struct {
    i2c_inst_t* i2c;             // Pointer to the instance of the I2C interface used
    uint8_t address;            // Sensor I2C address
    uint16_t time_timeout;      // Timeout for operations (in milliseconds)
    uint8_t stop_variable;     // Flag used to control the stopping of continuous measurements
    uint32_t measurement_time;   // Measurement time in microseconds
} vl53l0x_device;

// Function to initialize the VL53L0X sensor with the specified I2C interface
bool vl53l0x_boot(vl53l0x_device* device, i2c_inst_t* port_i2c);

// Function to start continuous measurements with interval defined in milliseconds
void vl53l0x_start_continuous(vl53l0x_device* device, uint32_t period_ms);

// Function to read the distance measured in continuous mode, returning the value in centimeters
uint16_t vl53l0x_reads_distance_from_sensor_cm(vl53l0x_device* device);

#endif // VL53L0X_H
