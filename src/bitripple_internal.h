/**
 * @file bitripple_internal.h
 * @brief Internal networking utilities for BitRipple
 *
 * Internal utility functions for UDP socket operations.
 * These are used by the client and server components.
 */

#ifndef BITRIPPLE_INTERNAL_H
#define BITRIPPLE_INTERNAL_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stddef.h>

/*======================================================================*
 * UDP Socket Operations                                                *
 *======================================================================*/

/**
 * @brief Create a new UDP socket
 * @return Socket file descriptor, or -1 on error
 */
int br_udp_socket_create(void);

/**
 * @brief Bind UDP socket to a port
 * @param fd Socket file descriptor
 * @param port Port number
 * @return 0 on success, -1 on error
 */
int br_udp_bind(int fd, uint16_t port);

/**
 * @brief Set socket to non-blocking mode
 * @param fd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int br_udp_set_nonblock(int fd);

/**
 * @brief Send data to a remote host
 * @param fd Socket file descriptor
 * @param buf Data buffer
 * @param len Data length
 * @param host Remote hostname/IP
 * @param port Remote port
 * @return Bytes sent, or -1 on error
 */
ssize_t br_udp_sendto(int fd, const void* buf, size_t len,
                     const char* host, uint16_t port);

/**
 * @brief Receive data from a remote host
 * @param fd Socket file descriptor
 * @param buf Data buffer
 * @param len Buffer length
 * @param from Source address (output)
 * @return Bytes received, or -1 on error
 */
ssize_t br_udp_recvfrom(int fd, void* buf, size_t len,
                       struct sockaddr_in* from);

#endif /* BITRIPPLE_INTERNAL_H */