/**
 * @file    esp_rom_crc.h
 * @date    2026-09-06
 * @brief   An independent CRC-32, kept as the reference the firmware's own
 *          fw_crc32_le() is checked against (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_ROM_CRC_H
#define HOST_STUB_ESP_ROM_CRC_H

/* ------------------------------ Includes ------------------------------- */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   CRC-32 over a buffer, matching esp_rom_crc32_le() bit for bit.
 * @param   crc    running value; pass 0 to start, or a previous return value
 *                 to continue over a second buffer
 * @param   buf    bytes to fold in; may be NULL only when `len` is 0
 * @param   len    number of bytes
 * @return  The CRC-32 of the bytes so far.
 * @note    This is a DELIBERATELY INDEPENDENT implementation - a bitwise
 *          reflected CRC-32 written from the polynomial, not a copy of the
 *          table-driven one in `middleware/fw`. **That independence is now the
 *          only reason this file still exists.** No firmware source includes
 *          `esp_rom_crc.h` any more - `fw_crc32_le()` replaced it - so nothing
 *          here is shadowing a vendor header. What it does instead is give the
 *          tests a second opinion: the protocol tests build their frames with
 *          this function while the parser checks them with `fw_crc32_le()`, so
 *          the two are only interchangeable if they agree, and
 *          `test_fw_crc32_agrees_with_an_independent_implementation` compares
 *          them directly over 256 lengths. The known-answer vectors in
 *          test_protocol.c and test_fw.c pin both to a value fixed outside
 *          either - without which a wrong CRC would agree with itself and pass
 *          everything.
 *
 *          The ROM routine seeds with ~crc and returns ~crc, so passing 0
 *          yields the standard reflected CRC-32 (polynomial 0xEDB88320, init
 *          and xorout 0xFFFFFFFF) - the zlib and Ethernet CRC the wire format
 *          asks for. This fake reproduces that convention exactly, including
 *          the double inversion, so a continued call chains the same way.
 */
static inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    uint32_t value = ~crc;

    for (uint32_t i = 0; i < len; ++i) {
        value ^= (uint32_t) buf[i];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t) - (int32_t) (value & 1U);
            value = (value >> 1) ^ (0xEDB88320U & mask);
        }
    }

    return ~value;
}

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_ROM_CRC_H */

/*** end of file ***/
