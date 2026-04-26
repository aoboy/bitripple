/**
 * @file bitripple.h
 * @brief Public API for the BitRipple library
 *
 * This file contains the public API for the BitRipple library,
 * which provides multi-path streaming with adaptive spray routing
 * and feedback-driven path quality optimization.
 */

#ifndef BITRIPPLE_H
#define BITRIPPLE_H

#include "bitripple_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*======================================================================*
 * Library Lifecycle                                                    *
 *======================================================================*/

/**
 * @brief Initialize the BitRipple library
 * @return BR_OK on success, error code on failure
 */
int br_init(void);

/**
 * @brief Cleanup and release library resources
 */
void br_cleanup(void);

/*======================================================================*
 * Profile Management                                                  *
 *======================================================================*/

/**
 * @brief Create a new profile with given path count and ball count
 * @param n_paths Number of parallel paths
 * @param m_balls Total balls (symbols) to distribute
 * @return New profile, or NULL on error
 */
br_profile_t* br_profile_new(uint32_t n_paths, uint32_t m_balls);

/**
 * @brief Free a profile
 * @param p Profile to free
 */
void br_profile_free(br_profile_t* p);

/**
 * @brief Set uniform ball distribution across all paths
 * @param p Profile to modify
 * @return BR_OK on success
 */
int br_profile_set_uniform(br_profile_t* p);

/**
 * @brief Set distribution based on fractional weights
 * @param p Profile to modify
 * @param frac Fractional weight for each path (sum should <= 1.0)
 * @return BR_OK on success
 */
int br_profile_set_fractions(br_profile_t* p, const double* frac);

/*======================================================================*
 * Profile Updates (Embodiment Implementations)                             *
 *======================================================================*/

/**
 * @brief Embodiment 1: Whack a path, redistribute evenly to others
 * @param p Profile to update
 * @param path Path index to reduce
 * @param alpha Fraction of balls to remove (0.0 to 1.0)
 * @return BR_OK on success
 */
int br_profile_update_whack(br_profile_t* p, uint32_t path, double alpha);

/**
 * @brief Embodiment 2: Redistribute evenly to specified paths
 * @param p Profile to update
 * @param e Array of path indices to redistribute to
 * @param n Number of paths in array
 * @return BR_OK on success
 */
int br_profile_update_redistribute_even(br_profile_t* p,
                                        const uint32_t* e, uint32_t n);

/**
 * @brief Embodiment 2 variant: Redistribute to all paths except specified
 * @param p Profile to update
 * @param e Array of path indices to exclude
 * @param n Number of paths in array
 * @return BR_OK on success
 */
int br_profile_update_redistribute_exclude(br_profile_t* p,
                                        const uint32_t* e, uint32_t n);

/**
 * @brief Embodiment 4: Redistribute proportionally to path quality
 * @param p Profile to update
 * @param e Array of path indices to redistribute to
 * @param n Number of paths in array
 * @return BR_OK on success
 */
int br_profile_update_redistribute_proportional(br_profile_t* p,
                                               const uint32_t* e, uint32_t n);

/*======================================================================*
 * Spray Routing                                                        *
 *======================================================================*/

/**
 * @brief Create a new spray state
 * @param l Spray length in bits
 * @param sa Starting value
 * @param sb Step value
 * @param method Spray method (1=LRRT, 2=RLRT)
 * @return New spray state, or NULL on error
 */
br_spray_t* br_spray_new(uint32_t l, uint32_t sa, uint32_t sb, int method);

/**
 * @brief Free spray state
 * @param s Spray to free
 */
void br_spray_free(br_spray_t* s);

/**
 * @brief Select path for given spray index
 * @param p Profile to use
 * @param s Spray state
 * @param j Spray index
 * @return Selected path index
 */
uint32_t br_spray_select(const br_profile_t* p, br_spray_t* s, uint32_t j);

/**
 * @brief Get next path and advance spray state
 * @param p Profile to use
 * @param s Spray state
 * @return Selected path index
 */
uint32_t br_spray_next(br_profile_t* p, br_spray_t* s);

/*======================================================================*
 * Codec (Encoding/Decoding)                                             *
 *======================================================================*/

/** Forward declaration for encoder */
typedef struct br_encoder br_encoder_t;

/** Forward declaration for decoder */
typedef struct br_decoder br_decoder_t;

/**
 * @brief Create a new encoder
 * @param data Input data to encode
 * @param len Length of data in bytes
 * @param symbol_size Symbol size in bytes
 * @return New encoder, or NULL on error
 */
br_encoder_t* br_encoder_new(const void* data, size_t len, uint16_t symbol_size);

/**
 * @brief Free encoder
 * @param e Encoder to free
 */
void br_encoder_free(br_encoder_t* e);

/**
 * @brief Get number of encoding blocks
 * @param e Encoder
 * @return Number of blocks
 */
uint32_t br_encoder_num_blocks(br_encoder_t* e);

/**
 * @brief Get number of source symbols in a block
 * @param e Encoder
 * @param sbn Block number
 * @return Number of source symbols
 */
uint32_t br_encoder_block_symbols(br_encoder_t* e, uint8_t sbn);

/**
 * @brief Encode a symbol
 * @param e Encoder
 * @param sbn Block number
 * @param esi Encoding symbol index
 * @param buf Output buffer
 * @param out_len Output length
 * @return BR_OK on success
 */
int br_encoder_symbol(br_encoder_t* e, uint8_t sbn, uint32_t esi,
                      void* buf, size_t* out_len);

/**
 * @brief Get object transfer information
 * @param e Encoder
 * @param common Common OTI output
 * @param scheme Scheme-specific OTI output
 */
void br_encoder_oti(br_encoder_t* e, uint64_t* common, uint32_t* scheme);

/**
 * @brief Create a new decoder
 * @param oti_common Common OTI from encoder
 * @param oti_scheme Scheme-specific OTI from encoder
 * @param data_len Expected data length
 * @return New decoder, or NULL on error
 */
br_decoder_t* br_decoder_new(uint64_t oti_common, uint32_t oti_scheme,
                              size_t data_len);

/**
 * @brief Free decoder
 * @param d Decoder to free
 */
void br_decoder_free(br_decoder_t* d);

/**
 * @brief Feed a received symbol to decoder
 * @param d Decoder
 * @param sbn Block number
 * @param esi Encoding symbol index
 * @param buf Symbol data
 * @param len Symbol length
 * @return BR_OK on success
 */
int br_decoder_feed(br_decoder_t* d, uint8_t sbn, uint32_t esi,
                    const void* buf, size_t len);

/**
 * @brief Check if decoder has enough symbols to reconstruct
 * @param d Decoder
 * @return Non-zero if ready
 */
int br_decoder_ready(br_decoder_t* d);

/**
 * @brief Reconstruct original data
 * @param d Decoder
 * @param out Output buffer
 * @param len Buffer length
 * @return BR_OK on success
 */
int br_decoder_reconstruct(br_decoder_t* d, void* out, size_t len);

/*======================================================================*
 * Feedback-Driven Adaptation                                         *
 *======================================================================*/

/** Forward declaration for feedback context */
typedef struct br_feedback br_feedback_t;

/**
 * @brief Create a new feedback context
 * @param profile Profile to adapt
 * @param n_paths Number of paths
 * @return New feedback context, or NULL on error
 */
br_feedback_t* br_feedback_new(br_profile_t* profile, uint32_t n_paths);

/**
 * @brief Free feedback context
 * @param fb Feedback context to free
 */
void br_feedback_free(br_feedback_t* fb);

/**
 * @brief Apply severity classification to update profile
 * @param fb Feedback context
 * @param severity Array of severity levels per path
 * @return BR_OK on success
 */
int br_feedback_apply_severity(br_feedback_t* fb,
                               const br_severity_t* severity);

/**
 * @brief Apply ECN feedback for a path
 * @param fb Feedback context
 * @param path Path index
 * @return BR_OK on success
 */
int br_feedback_apply_ecn(br_feedback_t* fb, uint32_t path);

/**
 * @brief Apply loss feedback for a path
 * @param fb Feedback context
 * @param path Path index
 * @param lost_count Number of lost packets
 * @return BR_OK on success
 */
int br_feedback_apply_loss(br_feedback_t* fb, uint32_t path,
                           uint32_t lost_count);

/**
 * @brief Apply RTT measurement for a path
 * @param fb Feedback context
 * @param path Path index
 * @param rtt_usec Measured RTT in microseconds
 * @param baseline_usec Baseline RTT for comparison
 * @return BR_OK on success
 */
int br_feedback_apply_rtt(br_feedback_t* fb, uint32_t path,
                          uint32_t rtt_usec, uint32_t baseline_usec);

/**
 * @brief Classify severity from raw metrics
 * @param ecn_count ECN marks received
 * @param loss_count Packets lost
 * @param rtt_usec Measured RTT
 * @param baseline_usec Baseline RTT
 * @return Severity level
 */
br_severity_t br_feedback_classify(uint32_t ecn_count, uint32_t loss_count,
                                  uint32_t rtt_usec, uint32_t baseline_usec);

/*======================================================================*
 * Client API (Encoder + Spray + Feedback)                             *
 *======================================================================*/

/** Forward declaration for client */
typedef struct br_client br_client_t;

/**
 * @brief Create a new client
 * @param prof Profile to use
 * @param hosts Array of server hostnames/IPs
 * @param ports Array of server ports
 * @param n_paths Number of paths
 * @param symbol_size Symbol size in bytes
 * @param feedback_port Port to receive feedback
 * @return New client, or NULL on error
 */
br_client_t* br_client_new(const br_profile_t* prof,
                           const char** hosts,
                           const uint16_t* ports,
                           uint32_t n_paths,
                           uint16_t symbol_size,
                           uint16_t feedback_port);

/**
 * @brief Free client
 * @param c Client to free
 */
void br_client_free(br_client_t* c);

/**
 * @brief Send data using multipath spray
 * @param c Client
 * @param data Data to send
 * @param len Data length
 * @return BR_OK on success
 */
int br_client_send(br_client_t* c, const void* data, size_t len);

/**
 * @brief Poll for feedback and update profile
 * @param c Client
 * @param timeout_ms Timeout in milliseconds
 * @return Number of feedback packets processed
 */
int br_client_poll_feedback(br_client_t* c, int timeout_ms);

/*======================================================================*
 * Server API (Receiver + Decoder + Feedback)                            *
 *======================================================================*/

/** Forward declaration for server */
typedef struct br_server br_server_t;

/**
 * @brief Create a new server
 * @param port Port to listen on
 * @param expected_len Expected data length
 * @param symbol_size Symbol size in bytes
 * @param client_host Client hostname for feedback
 * @param client_feedback_port Client feedback port
 * @return New server, or NULL on error
 */
br_server_t* br_server_new(uint16_t port, size_t expected_len,
                          uint16_t symbol_size,
                          const char* client_host,
                          uint16_t client_feedback_port);

/**
 * @brief Free server
 * @param s Server to free
 */
void br_server_free(br_server_t* s);

/**
 * @brief Receive and decode data
 * @param s Server
 * @param out Output buffer
 * @param len Buffer length
 * @param timeout_ms Timeout in milliseconds
 * @return BR_OK on success
 */
int br_server_recv(br_server_t* s, void* out, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* BITRIPPLE_H */