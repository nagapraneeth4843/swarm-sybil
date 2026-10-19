/* protocol.h - shared wire format and timing constants.
 * Included by both honest_node and attacker_node.
 */
#pragma once
#include <stdint.h>

/* ---- Radio ---------------------------------------------------------- */
#define SWARM_CHANNEL       6          /* survey the band, pick 1/6/11   */
#define BCAST_PERIOD_MS     200        /* state broadcast interval       */
#define PER_ID_MS           10         /* delay between Sybil identities */
#define MIN_REST_MS         20         /* minimum rest between cycles */
#define UPDATE_PERIOD_MS    250        /* W-MSR update interval          */
#define TRUST_PERIOD_MS     1000       /* trust-matrix recompute interval*/
#define TELEM_PERIOD_MS     500        /* serial telemetry interval      */
#define STALE_TIMEOUT_US    1500000LL  /* drop neighbour after 1.5 s     */

/* ---- Sizing --------------------------------------------------------- */
#define MAX_TRACKED         16         /* max distinct source IDs        */
#define F_PARAM             1          /* W-MSR trim count               */
#define LAMBDA_ANCHOR       1.0f       /* weight on own sensor per update*/

/* ---- Packet --------------------------------------------------------- */
#define PKT_MAGIC           0x5753     /* 'SW' - reject foreign traffic  */

#define FLAG_HONEST         0x00
#define FLAG_SYBIL          0x01
#define FLAG_FORGED         0x02
#define FLAG_REPLAY         0x04

typedef struct __attribute__((packed)) {
    uint16_t magic;      /* PKT_MAGIC                                    */
    uint8_t  src_id[6];  /* claimed identity (spoofed by attacker)       */
    float    state;      /* consensus state x_i in [0,1]                 */
    float    sensor;     /* own measurement, drives the anchor           */
    uint8_t  flags;      /* ground truth for scoring; never read by      */
                         /* the defence logic itself                     */
    uint32_t seq;
    int64_t  t_us;
} swarm_pkt_t;           /* 29 bytes                                     */

_Static_assert(sizeof(swarm_pkt_t) == 29, "packet layout changed");
