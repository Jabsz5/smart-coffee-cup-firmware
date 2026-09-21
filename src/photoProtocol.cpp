#include "photoProtocol.h"

#include <cstring>

namespace {

// These declarations are private to photoProtocol.cpp.
constexpr uint8_t PACKET_START = 0x01;
constexpr uint8_t PACKET_DATA  = 0x02;
constexpr uint8_t PACKET_END   = 0x03;
constexpr uint8_t PACKET_ABORT = 0x04;

constexpr size_t IMAGE_BUFFER_CAPACITY =
    240U * 320U * 2U;

// Alignment is needed because the byte buffer is later treated
// as a uint16_t RGB565 buffer.
alignas(4) uint8_t imageBuffer[IMAGE_BUFFER_CAPACITY];

bool transferActive = false;

uint16_t imageWidth = 0;
uint16_t imageHeight = 0;

uint32_t expectedBytes = 0;
uint32_t receivedBytes = 0;

uint16_t expectedSequence = 0;
uint16_t receivedChunks = 0;


void abortImageTransfer() {
    transferActive = false;

    imageWidth = 0;
    imageHeight = 0;

    expectedBytes = 0;
    receivedBytes = 0;

    expectedSequence = 0;
    receivedChunks = 0;

    Serial.println("Image transfer aborted");
}


void handleStartPacket(const uint8_t* packet, size_t length) {
    if (length != 9) {
        Serial.printf("Invalid START packet length: %u\n", static_cast<unsigned>(length));
        return;
    }

    const uint16_t width = (static_cast<uint16_t>(packet[1]) << 8) | packet[2];

    const uint16_t height = (static_cast<uint16_t>(packet[3]) << 8) | packet[4];

    const uint32_t totalBytes =
        (static_cast<uint32_t>(packet[5]) << 24) |
        (static_cast<uint32_t>(packet[6]) << 16) |
        (static_cast<uint32_t>(packet[7]) << 8) |
        static_cast<uint32_t>(packet[8]);

    const uint32_t calculatedBytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 2U;

    if (width == 0 || height == 0) {
        Serial.println("Image dimensions cannot be zero");
        return;
    }

    if (totalBytes != calculatedBytes) {
        Serial.printf("Image size mismatch: header=%lu, calculated=%lu\n", static_cast<unsigned long>(totalBytes), static_cast<unsigned long>(calculatedBytes));
        return;
    }

    if (totalBytes > IMAGE_BUFFER_CAPACITY) {
        Serial.printf("Image is too large: %lu-byte image, %u-byte buffer\n", static_cast<unsigned long>(totalBytes), static_cast<unsigned>(IMAGE_BUFFER_CAPACITY));
        return;
    }

    imageWidth = width;
    imageHeight = height;
    expectedBytes = totalBytes;

    receivedBytes = 0;
    receivedChunks = 0;
    expectedSequence = 0;

    transferActive = true;

    Serial.printf("START: %u x %u, expecting %lu bytes\n", imageWidth, imageHeight, static_cast<unsigned long>(expectedBytes));
}


void handleDataPacket(const uint8_t* packet, size_t length) {
    if (!transferActive) {
        Serial.println("DATA received without active transfer");
        return;
    }

    // Three header bytes plus at least one complete RGB565 pixel.
    if (length < 5) {
        Serial.println("DATA packet is too short");
        abortImageTransfer();
        return;
    }

    const uint16_t sequence = (static_cast<uint16_t>(packet[1]) << 8) | packet[2];

    if (sequence != expectedSequence) {
        Serial.printf("Sequence error: expected %u, received %u\n", expectedSequence, sequence);
        abortImageTransfer();
        return;
    }

    // Skip:
    // packet[0] = packet type
    // packet[1] = sequence high
    // packet[2] = sequence low
    const uint8_t* pixelData = packet + 3;
    const size_t pixelByteCount = length - 3;

    if ((pixelByteCount % 2) != 0) {
        Serial.println("DATA contains an incomplete RGB565 pixel");

        abortImageTransfer();
        return;
    }

    if (receivedBytes + pixelByteCount > expectedBytes) {
        Serial.println("DATA would exceed expected image size");
        abortImageTransfer();
        return;
    }

    if (receivedBytes + pixelByteCount > IMAGE_BUFFER_CAPACITY) {
        Serial.println( "DATA would overflow image buffer");

        abortImageTransfer();
        return;
    }

    memcpy(imageBuffer + receivedBytes, pixelData, pixelByteCount);

    receivedBytes += pixelByteCount;
    receivedChunks++;
    expectedSequence++;

    Serial.printf(
        "DATA #%u: copied %u pixel bytes. Total: %lu/%lu\n",
        sequence,
        static_cast<unsigned>(pixelByteCount),
        static_cast<unsigned long>(receivedBytes),
        static_cast<unsigned long>(expectedBytes)
    );
}


void handleEndPacket(const uint8_t* packet, size_t length, Adafruit_ST7789& display) {
    if (!transferActive) {
        Serial.println("END received without active transfer");
        return;
    }

    if (length != 3) {
        Serial.printf("Invalid END packet length: %u\n", static_cast<unsigned>(length));
        abortImageTransfer();
        return;
    }

    const uint16_t reportedChunks = (static_cast<uint16_t>(packet[1]) << 8) | packet[2];

    const bool byteCountCorrect = receivedBytes == expectedBytes;

    const bool chunkCountCorrect = receivedChunks == reportedChunks;

    if (!byteCountCorrect || !chunkCountCorrect) {
        Serial.println("Incomplete image transfer");

        Serial.printf("Bytes: received %lu, expected %lu\n", static_cast<unsigned long>(receivedBytes), static_cast<unsigned long>(expectedBytes));

        Serial.printf("Chunks: received %u, expected %u\n", receivedChunks, reportedChunks);

        abortImageTransfer();
        return;
    }

    transferActive = false;

    Serial.println("Image transfer completed successfully");

    Serial.println("Writing image to ST7789V...");

    const uint32_t pixelCount = static_cast<uint32_t>(imageWidth) * static_cast<uint32_t>(imageHeight);

    uint16_t* rgb565Pixels = reinterpret_cast<uint16_t*>(imageBuffer);

    display.startWrite();

    display.setAddrWindow(0, 0, imageWidth, imageHeight);

    display.writePixels(
        rgb565Pixels,
        pixelCount,
        true,  // Block until transfer completes
        true   // Source pixels use big-endian byte order
    );

    display.endWrite();
    Serial.println("Image displayed successfully");
}

} // namespace


void handleImagePacket(const uint8_t* packet, size_t packetLength, Adafruit_ST7789& display) {
    if (packet == nullptr || packetLength == 0) {
        Serial.println("Empty image packet");
        return;
    }

    const uint8_t packetType = packet[0];

    Serial.printf("Packet type: 0x%02X, packet length: %u\n", packetType, static_cast<unsigned>(packetLength));

    switch (packetType) {
        case PACKET_START:
            handleStartPacket(packet, packetLength);
            break;

        case PACKET_DATA:
            handleDataPacket(packet, packetLength);
            break;

        case PACKET_END:
            handleEndPacket(packet, packetLength, display);
            break;

        case PACKET_ABORT:
            abortImageTransfer();
            break;

        default:
            Serial.printf("Unknown packet type: 0x%02X\n", packetType);
            break;
    }
}