// Platform-independent API for IDE physical layer access.
// This is structured as a message passing interface.

#pragma once

#include <stdint.h>

enum ide_msg_type_t {
    // Messages from PHY to application
    IDE_MSG_NONE        = 0x00,
    IDE_MSG_RESET       = 0x01, // IDE bus has been reset by host
    IDE_MSG_CMD_START   = 0x02, // New command has started
    
    // Messages from application to PHY
    IDE_MSG_DEVICE_RDY  = 0x82, // Command done, device ready, unset BSY
    IDE_MSG_SEND_DATA   = 0x83, // Start transmitting data buffer to host
    IDE_MSG_RECV_DATA   = 0x84, // Start receiving data buffer from host
    
    // Codes 0xE0 to 0xFF are reserved for platform specific implementation
    IDE_MSG_PLATFORM    = 0xE0
};

enum ide_msg_status_t {
    IDE_MSGSTAT_IDLE        = 0x00,  // Message has not yet been queued
    IDE_MSGSTAT_QUEUED      = 0x01,  // Message is queued for execution
    IDE_MSGSTAT_EXECUTING   = 0x02,  // Message is currently executing

    IDE_MSGSTAT_DONE        = 0x80,  // Any type of done status will have highest bit set
    IDE_MSGSTAT_SUCCESS     = 0x83,  // Message was successful
    IDE_MSGSTAT_ABORTED     = 0x84,  // Message was aborted due to host activity
    IDE_MSGSTAT_ERROR       = 0x85   // Error during message handling
};

struct ide_phy_msg_t {
    ide_msg_type_t type;
    volatile ide_msg_status_t *status; // If not NULL, will get message status updates
    
    // Payload type depends on message type
    union {
        struct {
            uint8_t device_control;
        } reset;
        struct {
            uint8_t command;
            uint8_t device;
            uint8_t features;
            uint8_t sector_count;
            uint8_t lbalow;
            uint8_t lbamid;
            uint8_t lbahigh;
        } cmd_start;
        struct {
            // Error register value for latest command
            uint8_t error;

            // Override bits in status register
            uint8_t status;

            // For some commands the command block registers are set
            // to different value upon completion.
            bool set_registers;
            uint8_t sector_count;
            uint8_t device;
            uint8_t lbalow;
            uint8_t lbamid;
            uint8_t lbahigh;

            // Assert interrupt signal after register updates
            bool assert_irq;
        } device_rdy;

        struct {
            uint32_t words; // Number of 16-bit words to send
            const uint16_t *data;

            // Assert interrupt when ready to transfer
            bool assert_irq;
        } send_data;

        struct {
            uint32_t words; // Number of 16-bit words to receive
            uint16_t *data;

            // Assert interrupt when ready to transfer
            bool assert_irq;
        } recv_data;
    } payload;
};

// Reset the PHY layer and clear FIFOs
void ide_phy_reset();

// Returns one message from PHY FIFO, or NULL if empty.
// Returned pointer is valid until next call to this function.
ide_phy_msg_t *ide_phy_get_msg();

// Puts a message to be executed by PHY.
// This function takes internal copy of the message, so the message struct can be immediately freed.
// Any memory block referred by pointers inside message must remain valid until *status has IDE_MSGSTAT_DONE set.
// Returns false if FIFO is full (PHY stuck)
bool ide_phy_send_msg(ide_phy_msg_t *msg);
