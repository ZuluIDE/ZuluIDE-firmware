// Access to external ICE5LP1K FPGA that is used to implement
// IDE bus communications.

#pragma once
#include <stdint.h>
#include <stdlib.h>

// Initialize FPGA and load bitstream
bool fpga_init();

// Send a write command to FPGA through QSPI bus
// If keep_active is true, payload can be transferred using fpga_wrdata_start().
void fpga_wrcmd(uint8_t cmd, const uint8_t *payload, size_t payload_len, bool keep_active = false);

// Send a read command to FPGA through QSPI bus
// If keep_active is true, payload can be transferred using fpga_rddata_start().
void fpga_rdcmd(uint8_t cmd, uint8_t *result, size_t result_len, bool keep_active = false);

// Start a data transfer to/from FPGA using DMA
void fpga_wrdata_start(const uint32_t *data, size_t num_words);
void fpga_rddata_start(uint32_t *data, size_t num_words);

// Wait for DMA data transfer to finish
void fpga_transfer_finish();
