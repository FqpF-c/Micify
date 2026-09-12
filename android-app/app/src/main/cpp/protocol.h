#ifndef MICIFY_PROTOCOL_H
#define MICIFY_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define MICIFY_MAGIC 0x4649434Du /* "MICF" */
#define MICIFY_VERSION 1
#define MICIFY_FLAG_FORMAT 0x1u

#define MICIFY_MAX_PACKET 1500 /* stay under typical MTU, no IP fragmentation */

#define MICIFY_DISCOVERY_MAGIC 0x4449434Du /* "MICD" */
#define MICIFY_DISCOVERY_VERSION 1
#define MICIFY_DISCOVERY_PORT 44552
#define MICIFY_DISCOVERY_NAME_LEN 32
#define MICIFY_DISCOVERY_INTERVAL_MS 1000

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

/* Broadcast every MICIFY_DISCOVERY_INTERVAL_MS to 255.255.255.255:
 * MICIFY_DISCOVERY_PORT so phones never need a manually-typed IP - see
 * docs/PROTOCOL.md "Discovery beacon". Separate port from the audio
 * stream so it never reaches the strict audio packet parser. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t audio_port;
    char name[MICIFY_DISCOVERY_NAME_LEN];
} micify_beacon_t;

#pragma pack(pop)

#define MICIFY_HEADER_SIZE 16
#define MICIFY_FORMAT_SIZE 12
#define MICIFY_BEACON_SIZE 40

_Static_assert(sizeof(micify_header_t) == MICIFY_HEADER_SIZE, "header must be 16 bytes on the wire");
_Static_assert(sizeof(micify_format_t) == MICIFY_FORMAT_SIZE, "format record must be 12 bytes on the wire");
_Static_assert(sizeof(micify_beacon_t) == MICIFY_BEACON_SIZE, "beacon must be 40 bytes on the wire");

#endif /* MICIFY_PROTOCOL_H */
