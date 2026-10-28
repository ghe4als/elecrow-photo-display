#include <Arduino.h>
#include <Wire.h>

#include "crowpanel/touch.h"
#include "crowpanel/display.h"

/*---------------------------------------------------------------
 * PCA9557 and GT911 low-level access
 * The expander provides the reset sequence required before probing GT911.
 *--------------------------------------------------------------*/
namespace {
constexpr uint8_t pca9557_address = 0x18;
constexpr uint8_t pca9557_output = 0x01;
constexpr uint8_t pca9557_config = 0x03;
constexpr uint8_t gt911_addresses[] = {0x5D, 0x14};
constexpr uint16_t gt911_point_info = 0x814E;
constexpr uint16_t gt911_first_point = 0x814F;
constexpr uint16_t gt911_product_id = 0x8140;

uint8_t gt911_address = 0;

/** Read one PCA9557 register over I2C. */
uint8_t expander_read(uint8_t reg)
{
    Wire.beginTransmission(pca9557_address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(pca9557_address, static_cast<uint8_t>(1));
    return Wire.available() ? Wire.read() : 0xFF;
}

/** Write one PCA9557 register over I2C. */
void expander_write(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(pca9557_address);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

/** Read a register range from a specific GT911 I2C address. */
bool gt911_read_at(uint8_t address, uint16_t reg, uint8_t * data, size_t size)
{
    Wire.beginTransmission(address);
    Wire.write(highByte(reg));
    Wire.write(lowByte(reg));
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    const size_t received = Wire.requestFrom(address, size);
    if (received != size) {
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        data[index] = Wire.read();
    }
    return true;
}

/** Read from the address discovered during touch_init(). */
bool gt911_read(uint16_t reg, uint8_t * data, size_t size)
{
    return gt911_address && gt911_read_at(gt911_address, reg, data, size);
}

/** Acknowledge the GT911 status register after consuming its point data. */
void gt911_clear_status()
{
    Wire.beginTransmission(gt911_address);
    Wire.write(highByte(gt911_point_info));
    Wire.write(lowByte(gt911_point_info));
    Wire.write(0);
    Wire.endTransmission();
}

/** Apply the carrier-board reset sequence before probing GT911. */
void touch_reset()
{
    expander_write(pca9557_config, 0xFC);
    expander_write(pca9557_output, expander_read(pca9557_output) & 0xFC);
    delay(20);
    expander_write(pca9557_output, expander_read(pca9557_output) | 0x01);
    delay(100);
    expander_write(pca9557_config, expander_read(pca9557_config) | 0x02);
}
}

/**
 * @brief Reset the touch hardware and discover its active I2C address.
 */
void touch_init()
{
    touch_reset();
    uint8_t product_id[4];
    // Probe both common GT911 addresses because the reset level selects one.
    for (uint8_t address : gt911_addresses) {
        if (gt911_read_at(address, gt911_product_id, product_id, sizeof(product_id))) {
            gt911_address = address;
            Serial.printf("GT911 found at 0x%02X, ID %.4s\n", address, product_id);
            return;
        }
    }
    Serial.println("GT911 not found");
}

/**
 * @brief Read the first active point and map it to portrait logical coordinates.
 * @param x Output horizontal coordinate.
 * @param y Output vertical coordinate.
 * @return true when a valid point was decoded; false otherwise.
 */
bool touch_read_point(int16_t & x, int16_t & y)
{
    uint8_t status;
    if (!gt911_read(gt911_point_info, &status, 1) || !(status & 0x80)) {
        return false;
    }
    const uint8_t points = status & 0x0F;
    uint8_t point[7];
    const bool valid = points > 0 && points <= 5 && gt911_read(gt911_first_point, point, sizeof(point));
    gt911_clear_status();
    if (!valid) {
        return false;
    }
    const uint16_t raw_x = point[1] | (point[2] << 8);
    const uint16_t raw_y = point[3] | (point[4] << 8);
    const int16_t phys_x = constrain(raw_x, 0, crowpanel::panel_width - 1);
    const int16_t phys_y = constrain(raw_y, 0, crowpanel::panel_height - 1);

    x = phys_y;
    y = (crowpanel::panel_width - 1) - phys_x;

    x = constrain(x, 0, crowpanel::display_width - 1);
    y = constrain(y, 0, crowpanel::display_height - 1);
    return true;
}
