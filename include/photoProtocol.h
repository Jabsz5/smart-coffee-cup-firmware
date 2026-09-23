#ifndef PHOTO_PROTOCOL_H
#define PHOTO_PROTOCOL_H

#include <Arduino.h>
#include <Adafruit_ST7789.h>

#define PHOTO_PACKET_MAX_SIZE 244

struct PhotoPacketMessage {
    uint16_t length;
    uint8_t data[PHOTO_PACKET_MAX_SIZE];
};

constexpr uint8_t UPLOAD_PHOTO_COMMAND = 21;
constexpr uint8_t PHOTO_READY_ACK = 22;
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