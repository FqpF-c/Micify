#ifndef MICIFY_PROTOCOL_H
#define MICIFY_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define MICIFY_MAGIC 0x4649434Du /* "MICF" */
#define MICIFY_VERSION 1
#define MICIFY_FLAG_FORMAT 0x1u

#define MICIFY_MAX_PACKET 1500 /* stay under typical MTU, no IP fragmentation */

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t seq;
    uint32_t timestamp;
} micify_header_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t frame_ms;
    uint16_t reserved0;
    uint32_t reserved1;
} micify_format_t;

#pragma pack(pop)

#define MICIFY_HEADER_SIZE 16
#define MICIFY_FORMAT_SIZE 12

_Static_assert(sizeof(micify_header_t) == MICIFY_HEADER_SIZE, "header must be 16 bytes on the wire");
_Static_assert(sizeof(micify_format_t) == MICIFY_FORMAT_SIZE, "format record must be 12 bytes on the wire");

#endif /* MICIFY_PROTOCOL_H */
