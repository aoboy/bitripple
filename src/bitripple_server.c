#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include "bitripple.h"
#include "bitripple_internal.h"

/*======================================================================*
 * Internal Structure                                                 *
 *======================================================================*/

/**
 * @brief Server context
 *
 * Encapsulates decoder, network sockets, statistics,
 * and feedback destination for reception.
 */
struct br_server {
    int sockfd;                 /**< Main receive socket */
    int feedback_fd;            /**< Feedback send socket */
    br_decoder_t* dec;           /**< FEC decoder */
    size_t expected_len;        /**< Expected data length */
    uint16_t symbol_size;      /**< Symbol size in bytes */
    uint32_t path_seq_next[BR_MAX_PATHS]; /**< Next expected sequence */
    br_path_stats_t stats[BR_MAX_PATHS]; /**< Per-path statistics */

    /* Feedback destination */
    char client_host[64];
    uint16_t client_feedback_port;
    struct sockaddr_in client_addr;
    int client_addr_valid;
};

/*======================================================================*
 * Server Lifecycle                                                 *
 *======================================================================*/

/**
 * @brief Create a new server
 *
 * Initializes receive socket and feedback destination.
 *
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
                        uint16_t client_feedback_port)
{
    if (expected_len == 0 || symbol_size == 0) {
        return NULL;
    }

    br_server_t* s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }

    s->expected_len = expected_len;
    s->symbol_size = symbol_size;
    s->dec = NULL;
    s->client_addr_valid = 0;

    /* Create main receive socket */
    s->sockfd = br_udp_socket_create();
    if (s->sockfd < 0) {
        free(s);
        return NULL;
    }
    if (br_udp_bind(s->sockfd, port) < 0) {
        close(s->sockfd);
        free(s);
        return NULL;
    }

    /* Create feedback socket */
    s->feedback_fd = br_udp_socket_create();
    if (s->feedback_fd < 0) {
        close(s->sockfd);
        free(s);
        return NULL;
    }

    /* Store client feedback destination */
    if (client_host) {
        strncpy(s->client_host, client_host, sizeof(s->client_host) - 1);
        s->client_feedback_port = client_feedback_port;

        memset(&s->client_addr, 0, sizeof(s->client_addr));
        s->client_addr.sin_family = AF_INET;
        s->client_addr.sin_port = htons(client_feedback_port);
        inet_pton(AF_INET, client_host, &s->client_addr.sin_addr);
        s->client_addr_valid = 1;
    }

    return s;
}

/**
 * @brief Free server
 * @param s Server to free
 */
void br_server_free(br_server_t* s)
{
    if (!s) {
        return;
    }

    br_decoder_free(s->dec);
    if (s->sockfd >= 0) {
        close(s->sockfd);
    }
    if (s->feedback_fd >= 0) {
        close(s->feedback_fd);
    }
    free(s);
}

/*======================================================================*
 * Loss Detection                                                   *
 *======================================================================*/

/**
 * @brief Detect gaps in sequence numbers and send loss feedback
 *
 * Checks if the received sequence number has a gap from the
 * previous one, and sends feedback if loss is detected.
 *
 * @param s Server
 * @param hdr Packet header
 */
static void server_check_loss(br_server_t* s, const br_net_header_t* hdr)
{
    uint32_t path = hdr->path_id;
    uint32_t seq = hdr->path_seq;
    br_path_stats_t* st = &s->stats[path];

    if (st->packets_received > 0) {
        uint32_t expected = st->last_seq + 1;
        if (seq > expected) {
            uint32_t lost = seq - expected;
            st->packets_lost += lost;

            /* Send loss feedback */
            if (s->client_addr_valid) {
                br_feedback_pkt_t fb = {
                    .magic = BR_MAGIC,
                    .path_id = path,
                    .path_seq = expected,
                    .flags = BR_FLAG_LOSS,
                    .severity = BR_SEV_SEVERE,
                    .rtt_usec = 0,
                    .lost_seq = expected,
                    .lost_count = lost
                };
                sendto(s->feedback_fd, &fb, sizeof(fb), 0,
                      (struct sockaddr*)&s->client_addr, sizeof(s->client_addr));
            }
        }
    }

    st->last_seq = seq;
    st->packets_received++;
}

/*======================================================================*
 * ECN Detection                                                    *
 *======================================================================*/

/**
 * @brief Check ECN flag and send feedback
 *
 * Detects ECN marks in received packets and sends
 * feedback to the client.
 *
 * @param s Server
 * @param hdr Packet header
 */
static void server_check_ecn(br_server_t* s, const br_net_header_t* hdr)
{
    if (!(hdr->flags & BR_FLAG_ECN)) {
        return;
    }

    uint32_t path = hdr->path_id;
    s->stats[path].ecn_count++;

    if (s->client_addr_valid) {
        br_feedback_pkt_t fb = {
            .magic = BR_MAGIC,
            .path_id = path,
            .path_seq = hdr->path_seq,
            .flags = BR_FLAG_ECN,
            .severity = BR_SEV_MODERATE,
            .rtt_usec = 0,
            .lost_seq = 0,
            .lost_count = 0
        };
        sendto(s->feedback_fd, &fb, sizeof(fb), 0,
              (struct sockaddr*)&s->client_addr, sizeof(s->client_addr));
    }
}

/*======================================================================*
 * RTT Tracking                                                     *
 *======================================================================*/

/**
 * @brief Update RTT tracking from packet
 *
 * Records RTT measurements from incoming packets.
 * (Simplified: tracks one-way delay approximation.)
 *
 * @param s Server
 * @param hdr Packet header
 */
static void server_update_rtt(br_server_t* s, const br_net_header_t* hdr)
{
    if (!(hdr->flags & BR_FLAG_RTT_UPDATE)) {
        return;
    }

    uint32_t path = hdr->path_id;
    s->stats[path].rtt_sum_usec += hdr->rtt_usec;
    s->stats[path].rtt_samples++;
    s->stats[path].last_rtt_usec = hdr->rtt_usec;
}

/*======================================================================*
 * Data Reception                                                  *
 *======================================================================*/

/**
 * @brief Receive and decode data
 *
 * Receives symbols from all paths, feeds them to the
 * decoder, and reconstructs the original data.
 *
 * @param s Server
 * @param out Output buffer
 * @param len Buffer length
 * @param timeout_ms Timeout in milliseconds
 * @return BR_OK on success
 */
int br_server_recv(br_server_t* s, void* out, size_t len, int timeout_ms)
{
    if (!s || !out) {
        return BR_ERR_INVAL;
    }

    uint8_t* buf = malloc(sizeof(br_net_header_t) + s->symbol_size);
    if (!buf) {
        return BR_ERR_NOMEM;
    }

    struct pollfd pfd = {
        .fd = s->sockfd,
        .events = POLLIN
    };
    int64_t remain = timeout_ms;

    while (1) {
        /* Check if decoder is ready */
        if (s->dec && br_decoder_ready(s->dec)) {
            break;
        }

        int wait = (remain > 1000) ? 1000 : (int)remain;
        if (remain <= 0) {
            free(buf);
            return BR_ERR_TIMEOUT;
        }

        int n = poll(&pfd, 1, wait);
        if (n < 0) {
            free(buf);
            return BR_ERR_NET;
        }
        if (n == 0) {
            remain -= wait;
            continue;
        }

        /* Receive packet */
        struct sockaddr_in from;
        ssize_t r = br_udp_recvfrom(s->sockfd, buf,
                                   sizeof(br_net_header_t) + s->symbol_size,
                                   &from);
        if (r < (ssize_t)sizeof(br_net_header_t)) {
            continue;
        }

        /* Parse header */
        br_net_header_t hdr;
        memcpy(&hdr, buf, sizeof(hdr));
        if (hdr.magic != BR_MAGIC) {
            continue;
        }
        if (hdr.path_id >= BR_MAX_PATHS) {
            continue;
        }

        /* Validate symbol */
        size_t plen = r - sizeof(br_net_header_t);
        if (plen != hdr.symbol_size) {
            continue;
        }

        /* Process feedback triggers */
        server_check_loss(s, &hdr);
        server_check_ecn(s, &hdr);
        server_update_rtt(s, &hdr);

        /* Lazy decoder init */
        if (!s->dec) {
            s->dec = br_decoder_new(hdr.oti_common, hdr.oti_scheme,
                                s->expected_len);
            if (!s->dec) {
                free(buf);
                return BR_ERR_CODEC;
            }
        }

        /* Feed symbol to decoder */
        int rc = br_decoder_feed(s->dec, (uint8_t)hdr.block_id,
                                 hdr.symbol_id,
                                 buf + sizeof(br_net_header_t), plen);
        (void)rc;  /* Ignore return - we poll until ready */
    }

    free(buf);

    if (!s->dec) {
        return BR_ERR_TIMEOUT;
    }

    return br_decoder_reconstruct(s->dec, out, len);
}