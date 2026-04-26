#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/time.h>
#include "bitripple.h"
#include "bitripple_internal.h"

/*======================================================================*
 * Internal Structure                                                 *
 *======================================================================*/

/**
 * @brief Client context
 *
 * Encapsulates encoder, spray state, feedback handler,
 * and network sockets for multipath transmission.
 */
struct br_client {
    br_profile_t* prof;          /**< Ball distribution profile */
    br_spray_t* spray;           /**< Spray state */
    br_encoder_t* enc;           /**< FEC encoder */
    br_feedback_t* feedback;     /**< Feedback handler */
    int sockfd;                 /**< Main send socket */
    int feedback_fd;            /**< Feedback receive socket */
    uint32_t n_paths;           /**< Number of paths */
    char* hosts[BR_MAX_PATHS];   /**< Server hostnames */
    uint16_t ports[BR_MAX_PATHS];/**< Server ports */
    uint32_t path_seq[BR_MAX_PATHS]; /**< Per-path sequence numbers */
    uint16_t symbol_size;       /**< Symbol size in bytes */
    uint16_t feedback_port;     /**< Feedback receive port */

    /* RTT measurement state */
    struct timeval send_time[BR_MAX_PATHS];
};

/*======================================================================*
 * Client Lifecycle                                                 *
 *======================================================================*/

/**
 * @brief Create a new client
 *
 * Initializes encoder, spray generator, feedback handler,
 * and network sockets for all paths.
 *
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
                        uint16_t feedback_port)
{
    if (!prof || !hosts || !ports || n_paths == 0 || n_paths > BR_MAX_PATHS) {
        return NULL;
    }
    if (n_paths != prof->n) {
        return NULL;
    }

    br_client_t* c = calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }

    /* Copy profile */
    c->prof = br_profile_new(prof->n, prof->m);
    if (!c->prof) {
        free(c);
        return NULL;
    }
    memcpy(c->prof, prof, sizeof(*prof));

    /* Calculate spray length from ball count */
    uint32_t l = 0;
    uint32_t t = prof->m;
    while (t > 1) {
        t >>= 1;
        l++;
    }
    c->spray = br_spray_new(l, 0, 1, 1);
    if (!c->spray) {
        br_profile_free(c->prof);
        free(c);
        return NULL;
    }

    c->n_paths = n_paths;
    c->symbol_size = symbol_size;
    c->feedback_port = feedback_port;

    /* Store hosts and ports */
    for (uint32_t i = 0; i < n_paths; i++) {
        c->hosts[i] = strdup(hosts[i]);
        c->ports[i] = ports[i];
        c->path_seq[i] = 0;
    }

    /* Create main send socket */
    c->sockfd = br_udp_socket_create();
    if (c->sockfd < 0) {
        br_client_free(c);
        return NULL;
    }

    /* Create feedback receive socket */
    c->feedback_fd = br_udp_socket_create();
    if (c->feedback_fd < 0) {
        br_client_free(c);
        return NULL;
    }
    if (br_udp_bind(c->feedback_fd, feedback_port) < 0) {
        br_client_free(c);
        return NULL;
    }
    br_udp_set_nonblock(c->feedback_fd);

    /* Create feedback handler */
    c->feedback = br_feedback_new(c->prof, n_paths);
    if (!c->feedback) {
        br_client_free(c);
        return NULL;
    }

    return c;
}

/**
 * @brief Free client
 * @param c Client to free
 */
void br_client_free(br_client_t* c)
{
    if (!c) {
        return;
    }

    if (c->sockfd >= 0) {
        close(c->sockfd);
    }
    if (c->feedback_fd >= 0) {
        close(c->feedback_fd);
    }
    br_encoder_free(c->enc);
    br_feedback_free(c->feedback);
    br_spray_free(c->spray);
    br_profile_free(c->prof);

    for (uint32_t i = 0; i < c->n_paths; i++) {
        free(c->hosts[i]);
    }
    free(c);
}

/*======================================================================*
 * Feedback Processing                                              *
 *======================================================================*/

/**
 * @brief Poll for feedback and apply profile adjustments
 *
 * Waits for feedback packets and applies them to the
 * profile through the feedback handler.
 *
 * @param c Client
 * @param timeout_ms Timeout in milliseconds
 * @return Number of feedback packets processed
 */
int br_client_poll_feedback(br_client_t* c, int timeout_ms)
{
    if (!c || c->feedback_fd < 0) {
        return BR_ERR_INVAL;
    }

    br_feedback_pkt_t fb;
    struct sockaddr_in from;

    int64_t remain = timeout_ms;
    int processed = 0;

    while (remain > 0) {
        struct pollfd pfd = {
            .fd = c->feedback_fd,
            .events = POLLIN
        };
        int n = poll(&pfd, 1, (int)remain);
        if (n <= 0) {
            break;
        }

        ssize_t r = br_udp_recvfrom(c->feedback_fd, &fb, sizeof(fb), &from);
        if (r < (ssize_t)sizeof(fb)) {
            continue;
        }
        if (fb.magic != BR_MAGIC) {
            continue;
        }
        if (fb.path_id >= c->n_paths) {
            continue;
        }

        /* Apply feedback based on flags */
        if (fb.flags & BR_FLAG_LOSS) {
            br_feedback_apply_loss(c->feedback, fb.path_id, fb.lost_count);
            processed++;
        }
        if (fb.flags & BR_FLAG_ECN) {
            br_feedback_apply_ecn(c->feedback, fb.path_id);
            processed++;
        }
        if (fb.flags & BR_FLAG_RTT_UPDATE) {
            br_feedback_apply_rtt(c->feedback, fb.path_id,
                                  fb.rtt_usec, 1000);
            processed++;
        }

        remain -= 10;  /* Rough accounting */
    }

    return processed;
}

/*======================================================================*
 * Data Transmission                                                *
 *======================================================================*/

/**
 * @brief Send data using multipath spray
 *
 * Encodes the data with FEC, then spray-encodes
 * symbols across all paths. Polls for feedback
 * before and after sending.
 *
 * @param c Client
 * @param data Data to send
 * @param len Data length
 * @return BR_OK on success
 */
int br_client_send(br_client_t* c, const void* data, size_t len)
{
    if (!c || !data) {
        return BR_ERR_INVAL;
    }

    /* Poll any pending feedback before sending */
    br_client_poll_feedback(c, 0);

    /* Create encoder */
    if (c->enc) {
        br_encoder_free(c->enc);
        c->enc = NULL;
    }
    c->enc = br_encoder_new(data, len, c->symbol_size);
    if (!c->enc) {
        return BR_ERR_CODEC;
    }

    /* Get OTI for header */
    uint64_t oti_common = 0;
    uint32_t oti_scheme = 0;
    br_encoder_oti(c->enc, &oti_common, &oti_scheme);

    /* Allocate packet buffer */
    uint8_t* pkt = malloc(sizeof(br_net_header_t) + c->symbol_size);
    if (!pkt) {
        return BR_ERR_NOMEM;
    }

    uint32_t num_blocks = br_encoder_num_blocks(c->enc);

    /* Send all symbols */
    for (uint8_t sbn = 0; sbn < num_blocks; sbn++) {
        uint32_t num_src = br_encoder_block_symbols(c->enc, sbn);
        uint32_t total = num_src + (num_src / 2);
        if (total < num_src + 4) {
            total = num_src + 4;
        }

        for (uint32_t i = 0; i < total; i++) {
            uint32_t esi = i;
            uint32_t path = br_spray_next(c->prof, c->spray);
            size_t sym_len = 0;

            int rc = br_encoder_symbol(c->enc, sbn, esi,
                                     pkt + sizeof(br_net_header_t), &sym_len);
            if (rc != BR_OK) {
                free(pkt);
                return rc;
            }

            /* Record send time for RTT estimation */
            gettimeofday(&c->send_time[path], NULL);

            /* Build header */
            br_net_header_t hdr = {
                .magic       = BR_MAGIC,
                .block_id    = sbn,
                .symbol_id   = esi,
                .path_id    = path,
                .path_seq   = c->path_seq[path]++,
                .symbol_size = (uint16_t)sym_len,
                .flags      = 0,
                .oti_common = oti_common,
                .oti_scheme = oti_scheme,
                .rtt_usec  = 0
            };
            memcpy(pkt, &hdr, sizeof(hdr));

            /* Send to selected path */
            rc = (int)br_udp_sendto(c->sockfd, pkt, sizeof(hdr) + sym_len,
                                    c->hosts[path], c->ports[path]);
            if (rc < 0) {
                free(pkt);
                return BR_ERR_NET;
            }
        }
    }

    free(pkt);

    /* Poll feedback after sending */
    br_client_poll_feedback(c, 100);

    return BR_OK;
}