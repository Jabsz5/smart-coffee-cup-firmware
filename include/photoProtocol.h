#ifndef PHOTO_PROTOCOL_H
#define PHOTO_PROTOCOL_H

#include <Arduino.h>
#include <Adafruit_ST7789.h>

/**
 * Processes one complete BLE photo-protocol packet.
 *
 * packet:
 *     Pointer to the characteristic value received from BLE.
 *
 * packetLength:
 *     Number of bytes in the characteristic value.
 *
 * display:
 *     Existing initialized ST7789 display object.
 */
void handleImagePacket(
    const uint8_t* packet,
    size_t packetLength,
    Adafruit_ST7789& display
);

#endif