/**
 * @file bitripple_types.h
 * @brief Type definitions and constants for the BitRipple library
 *
 * This file contains all shared type definitions, constants, and error codes
 * used across the BitRipple library components.
 */

#ifndef BITRIPPLE_TYPES_H
#define BITRIPPLE_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*======================================================================*
 * Constants                                                             *
 *======================================================================*/

/** Maximum number of concurrent paths supported */
#define BR_MAX_PATHS       32

/** Maximum symbol size in bytes (UDP payload limit) */
#define BR_MAX_SYMBOL_SIZE 65535

/** Magic number for packet validation ('BRPL' = 0x4252504C) */
#define BR_MAGIC           0x4252504C

/* Feedback flags embedded in network packet header */
#define BR_FLAG_ECN        0x0001  /* ECN (Explicit Congestion Notification) experienced */
#define BR_FLAG_LOSS       0x0002  /* Gap detected in path sequence */
#define BR_FLAG_RTT_UPDATE 0x0004 /* RTT measurement included in packet */

/*======================================================================*
 * Enumerations                                                          *
 *======================================================================*/

/**
 * @brief Severity levels for path quality degradation
 *
 * Used to classify the severity of network conditions on each path.
 * Higher severity indicates more severe degradation requiring
 * stronger corrective action in the spray profile.
 */
typedef enum {
    BR_SEV_NONE = 0,     /**< No degradation detected */
    BR_SEV_MILD = 1,     /**< Slight degradation, elevated RTT */
    BR_SEV_MODERATE = 2, /**< Frequent ECN marking or high RTT */
    BR_SEV_SEVERE = 3,   /**< Packet loss detected on path */
    BR_SEV_CRITICAL = 4  /**< Path unusable, should be avoided */
} br_severity_t;

/**
 * @brief Error codes returned by library functions
 */
typedef enum {
    BR_OK = 0,
    BR_ERR_NOMEM = -1,   /* Memory allocation failed */
    BR_ERR_INVAL = -2,    /* Invalid parameter */
    BR_ERR_CODEC = -3,    /* Codec encoding/decoding error */
    BR_ERR_NET   = -4,   /* Network operation error */
    BR_ERR_TIMEOUT = -5   /* Operation timed out */
} br_err_t;

/*======================================================================*
 * Data Structures                                                      *
 *======================================================================*/

/**
 * @brief Ball distribution profile for spray routing
 *
 * Stores the ball count allocation across N parallel paths.
 * The 'cum' array stores cumulative sums for O(log n) path selection.
 */
typedef struct br_profile {
    uint32_t n;           /**< Number of paths */
    uint32_t m;           /**< Total balls (symbols to send) */
    uint32_t balls[BR_MAX_PATHS];   /**< Balls allocated per path */
    uint32_t cum[BR_MAX_PATHS];   /**< Cumulative ball sums */
    uint32_t residual;    /**< Residual from uniform distribution */
} br_profile_t;

/**
 * @brief Spray state for path selection
 *
 * Maintains the state for generating the next path using
 * a configurable spray method (LRRT or RLRT).
 */
typedef struct br_spray {
    uint32_t j;       /**< Current spray index */
    uint32_t l;       /**< Spray length in bits (log2 of spray factor) */
    uint32_t sa;       /**< Starting value */
    uint32_t sb;       /**< Step value */
    int      method;   /**< Spray method: 1=LRRT, 2=RLRT */
} br_spray_t;

/**
 * @brief Network packet header
 *
 * Prepended to each symbol sent over the network.
 * Contains all metadata needed for routing and decoding.
 */
typedef struct br_net_header {
    uint32_t magic;          /**< Packet magic (BR_MAGIC) */
    uint32_t block_id;       /**< Encoding block number */
    uint32_t symbol_id;     /**< Encoding symbol index (ESI) */
    uint32_t path_id;        /**< Path this symbol was sent on */
    uint32_t path_seq;       /**< Per-path sequence number */
    uint16_t symbol_size;   /**< Size of this symbol in bytes */
    uint16_t flags;         /**< Feedback flags (ECN, LOSS, etc.) */
    uint64_t oti_common;    /**< Common object transfer information */
    uint32_t oti_scheme;    /**< Scheme-specific OTI */
    uint32_t rtt_usec;       /**< RTT measurement (optional) */
} __attribute__((packed)) br_net_header_t;

/**
 * @brief Feedback packet from server to client
 *
 * Sent periodically or on loss/ECN to report path conditions.
 */
typedef struct br_feedback_pkt {
    uint32_t magic;       /**< Magic (BR_MAGIC) */
    uint32_t path_id;     /**< Path this feedback applies to */
    uint32_t path_seq;   /**< Sequence number where issue detected */
    uint16_t flags;      /**< ECN/LOSS/RTT_UPDATE flags */
    uint16_t severity;   /**< Severity level */
    uint32_t rtt_usec;  /**< Measured RTT */
    uint32_t lost_seq;  /**< First missing sequence number */
    uint32_t lost_count; /**< Consecutive lost packet count */
} __attribute__((packed)) br_feedback_pkt_t;

/**
 * @brief Per-path statistics maintained by server
 *
 * Tracks receive statistics for loss and congestion detection.
 */
typedef struct br_path_stats {
    uint32_t packets_received;    /**< Total packets received */
    uint32_t packets_lost;        /**< Total packets detected lost */
    uint32_t last_seq;            /**< Last sequence number seen */
    uint32_t ecn_count;          /**< ECN marks received */
    uint64_t rtt_sum_usec;       /**< Sum of RTT samples */
    uint32_t rtt_samples;        /**< Number of RTT samples */
    uint32_t last_rtt_usec;      /**< Most recent RTT */
} br_path_stats_t;

/*======================================================================*
 * No close of extern "C" guard needed - ends with endif                      *
 *======================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* BITRIPPLE_TYPES_H */