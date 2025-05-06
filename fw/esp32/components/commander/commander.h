#ifndef COMMANDER_H
#define COMMANDER_H

#include <stdint.h>

#include "esp_err.h"

#define HEADER_LEN sizeof(wulpus_command_header_t)

typedef enum
{
    SET_CONFIG = 0x57,
    GET_DATA = 0x58,
    PING = 0x59,
    PONG = 0x5A,
    RESET = 0x5B,
    CLOSE = 0x5C,
} wulpus_command_type_e;

typedef struct __attribute__((packed))
{
    char magic[6];             // Magic string "wulpus"
    uint8_t command : 8;       // Command type
    uint16_t data_length : 16; // Length of data
} wulpus_command_header_t;

typedef struct
{
    uint8_t *data;        // Pointer to data buffer
    uint16_t data_length; // Length of data
} wulpus_command_data_t;

#endif
