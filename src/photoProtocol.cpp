#include "photoProtocol.h"
#include "displayController.h"
#include <atomic>
#include <cstring>
#include <cstdlib>

// Keep the image buffer reserved until drawing finishes.
static std::atomic<bool> photoPending{false};

bool isPhotoBufferReserved() {
    return photoPending.load();
}

namespace {

// These declarations are private to photoProtocol.cpp.
constexpr uint8_t PACKET_START = 0x01;
constexpr uint8_t PACKET_DATA  = 0x02;
constexpr uint8_t PACKET_END   = 0x03;
constexpr uint8_t PACKET_ABORT = 0x04;

// Info for image buffer management and transfer state.
constexpr uint16_t MAX_IMAGE_WIDTH = 240;
constexpr uint16_t MAX_IMAGE_HEIGHT = 320;

uint16_t* rowBuffer = nullptr;

uint16_t bufferedPixels = 0;
uint16_t nextRow = 0;

void releaseRowBuffer() {
    free(rowBuffer);
    rowBuffer = nullptr;

    bufferedPixels = 0;
    nextRow = 0;
}


bool transferActive = false;

uint16_t imageWidth = 0;
uint16_t imageHeight = 0;

uint32_t expectedBytes = 0;
uint32_t receivedBytes = 0;

uint16_t expectedSequence = 0;
uint16_t receivedChunks = 0;



void abortImageTransfer() {
    transferActive = false;
    releaseRowBuffer();

    imageWidth = 0;
    imageHeight = 0;

    expectedBytes = 0;
    receivedBytes = 0;

    expectedSequence = 0;
    receivedChunks = 0;

    Serial.println("Image transfer aborted");
}


void handleStartPacket(const uint8_t* packet, size_t length, Adafruit_ST7789& display) {
    if (transferActive) {
        Serial.println("START rejected: transfer already active");
        return;
    }

    if (packet == nullptr || length != 9) {
        Serial.printf("Invalid START packet: length=%u\n", static_cast<unsigned>(length));
        return;
    }

    const uint16_t width = (static_cast<uint16_t>(packet[1]) << 8) | packet[2];

    const uint16_t height = (static_cast<uint16_t>(packet[3]) << 8) | packet[4];

    const uint32_t totalBytes =
        (static_cast<uint32_t>(packet[5]) << 24) |
        (static_cast<uint32_t>(packet[6]) << 16) |
        (static_cast<uint32_t>(packet[7]) << 8) |
        static_cast<uint32_t>(packet[8]);

    if (width == 0 || height == 0 || width > MAX_IMAGE_WIDTH || height > MAX_IMAGE_HEIGHT || width > display.width() || height > display.height()) {
        Serial.printf("Invalid image dimensions: %u x %u\n", width, height);
        return;
    }

    const uint32_t calculatedBytes = static_cast<uint32_t>(width) * height * 2U;

    if (totalBytes != calculatedBytes) {
        Serial.printf("Image size mismatch: header=%lu, calculated=%lu\n", static_cast<unsigned long>(totalBytes), static_cast<unsigned long>(calculatedBytes));
        return;
    }

    // Clear any leftover allocation before starting a new transfer.
    releaseRowBuffer();

    const size_t rowBytes = static_cast<size_t>(width) * sizeof(uint16_t);

    rowBuffer = static_cast<uint16_t*>(malloc(rowBytes));

    if (rowBuffer == nullptr) {
        Serial.printf("Failed to allocate %u-byte row buffer\n", static_cast<unsigned>(rowBytes));
        return;
    }

    imageWidth = width;
    imageHeight = height;
    expectedBytes = totalBytes;

    receivedBytes = 0;
    receivedChunks = 0;
    expectedSequence = 0;

    bufferedPixels = 0;
    nextRow = 0;

    currentDisplayMode = DisplayMode::Photo;
    transferActive = true;

    Serial.printf("START: %u x %u, expecting %lu bytes, allocated %u-byte row buffer\n", imageWidth, imageHeight, static_cast<unsigned long>(expectedBytes), static_cast<unsigned>(rowBytes));
}

void handleDataPacket(const uint8_t* packet, size_t length, Adafruit_ST7789& display) {
    if (!transferActive) {
        Serial.println("DATA received without active transfer");
        return;
    }

    if (rowBuffer == nullptr) {
        Serial.println("DATA received without allocated row buffer");
        abortImageTransfer();
        return;
    }

    // Three header bytes plus at least one complete RGB565 pixel.
    if (packet == nullptr || length < 5) {
        Serial.println("DATA packet is null or too short");
        abortImageTransfer();
        return;
    }

    const uint16_t sequence =
        (static_cast<uint16_t>(packet[1]) << 8) | packet[2];

    if (sequence != expectedSequence) {
        Serial.printf("Sequence error: expected %u, received %u\n", expectedSequence, sequence);
        abortImageTransfer();
        return;
    }

    const uint8_t* pixelData = packet + 3;
    const size_t pixelByteCount = length - 3;

    if ((pixelByteCount % 2U) != 0) {
        Serial.println("DATA contains an incomplete RGB565 pixel");
        abortImageTransfer();
        return;
    }

    if (
        receivedBytes > expectedBytes ||
        pixelByteCount > expectedBytes - receivedBytes
    ) {
        Serial.println("DATA would exceed expected image size");
        abortImageTransfer();
        return;
    }

    for (size_t i = 0; i < pixelByteCount; i += 2) {
        // Check bounds before accessing the row buffer.
        if (
            imageWidth == 0 ||
            bufferedPixels >= imageWidth ||
            nextRow >= imageHeight
        ) {
            Serial.println("Invalid row buffer position");
            abortImageTransfer();
            return;
        }

        // Convert the phone's high-byte-first RGB565 data
        // into a native uint16_t pixel value.
        rowBuffer[bufferedPixels++] =
            (static_cast<uint16_t>(pixelData[i]) << 8) |
            static_cast<uint16_t>(pixelData[i + 1]);

        if (bufferedPixels == imageWidth) {
            display.startWrite();
            display.setAddrWindow(0, nextRow, imageWidth, 1);
            display.writePixels(rowBuffer, imageWidth, true, false);
            display.endWrite();

            // The blocking write finished; reuse the buffer.
            bufferedPixels = 0;
            nextRow++;
        }
    }

    receivedBytes += static_cast<uint32_t>(pixelByteCount);
    receivedChunks++;
    expectedSequence++;
}


void handleEndPacket(const uint8_t* packet, size_t length) {
    if (!transferActive) {
        Serial.println("END received without active transfer");
        return;
    }

    if (packet == nullptr || length != 3) {
        Serial.printf("Invalid END packet: length=%u\n", static_cast<unsigned>(length));
        abortImageTransfer();
        return;
    }

    const uint16_t reportedChunks = (static_cast<uint16_t>(packet[1]) << 8) | packet[2];

    const bool byteCountCorrect = receivedBytes == expectedBytes;

    const bool chunkCountCorrect = receivedChunks == reportedChunks;

    const bool rowsCorrect = nextRow == imageHeight && bufferedPixels == 0;

    if (rowBuffer == nullptr || !byteCountCorrect || !chunkCountCorrect || !rowsCorrect) {
        Serial.println("Incomplete image transfer");
        Serial.printf("Bytes: received %lu, expected %lu\n", static_cast<unsigned long>(receivedBytes), static_cast<unsigned long>(expectedBytes));
        Serial.printf("Chunks: received %u, expected %u\n", receivedChunks, reportedChunks);
        Serial.printf("Rows: drawn %u, expected %u, buffered pixels %u\n", nextRow, imageHeight, bufferedPixels);

        abortImageTransfer();
        return;
    }

    transferActive = false;

    // All blocking row writes have finished.
    releaseRowBuffer();

    Serial.println("Image transfer completed successfully; image displayed and row buffer freed");
}

} // namespace



void handleImagePacket(const uint8_t* packet, size_t packetLength, Adafruit_ST7789& display) {
    if (packet == nullptr || packetLength == 0) {
        Serial.println("Empty image packet");
        return;
    }

    const uint8_t packetType = packet[0];

    // Serial.printf("Packet type: 0x%02X, packet length: %u\n", packetType, static_cast<unsigned>(packetLength));

    switch (packetType) {
        case PACKET_START:
            handleStartPacket(packet, packetLength, display);
            break;

        case PACKET_DATA:
            handleDataPacket(packet, packetLength, display);
            break;

        case PACKET_END:
            handleEndPacket(packet, packetLength);
            break;

        case PACKET_ABORT:
            if (photoPending.load()) {
                Serial.println("ABORT received: photo is awaiting display or being drawn");
                break;
            } 
            abortImageTransfer();
            break;

        default:
            Serial.printf("Unknown packet type: 0x%02X\n", packetType);
            break;
    }
}