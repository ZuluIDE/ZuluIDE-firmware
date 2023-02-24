// Platform-independent API for IDE physical layer access.
// This is structured as a message passing interface.

#pragma once

#include <stdint.h>

enum ide_msg_type_t {
    // Messages from PHY to application
    IDE_MSG_NONE        = 0x00,
    IDE_MSG_RESET       = 0x01, // IDE bus has been reset by host
    IDE_MSG_CMD_START   = 0x02, // New command has started
    IDE_MSG_SEND_DONE   = 0x03, // Previous SEND_DATA request has completed
    IDE_MSG_RECV_DONE   = 0x04, // Previous RECV_DATA request has completed

    // Messages from application to PHY
    IDE_MSG_CMD_DONE    = 0x82, // Command done, unset BSY
    IDE_MSG_SEND_DATA   = 0x83, // Start transmitting data buffer to host
    IDE_MSG_RECV_DATA   = 0x84, // Start receiving data buffer from host
};

struct ide_phy_msg_t {
    ide_msg_type_t type;

    // Payload type depends on message type
    union {
        struct {
            uint8_t command;
            uint8_t device;
            uint8_t features;
            uint8_t sector_count;
            uint32_t lba;
        } cmd_start;

        struct {
            uint8_t error;
        } cmd_done;

        struct {
            uint32_t length;
            const uint8_t *data;
        } send_data;

        struct {
            uint32_t length;
            uint8_t *data;
        } recv_data;
    } payload;
};

// Reset the PHY layer and clear FIFOs
void ide_phy_reset();

// Returns one message from PHY FIFO, or NULL if empty.
// Returned pointer is valid until next call to this function.
ide_phy_msg_t *ide_phy_get_msg();

// Puts a message to be executed by PHY.
// This function takes internal copy of the message.
// Returns false if FIFO is full (PHY stuck)
bool ide_phy_send_msg(ide_phy_msg_t *msg);
