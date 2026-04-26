/**
 * @file bitripple_net.c
 * @brief UDP networking utilities implementation
 *
 * Internal networking utilities for UDP socket operations.
 * Used by client and server components for data transfer.
 */

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "bitripple_internal.h"

/*======================================================================*
 * Socket Creation                                                    *
 *======================================================================*/

/**
 * @brief Create a UDP socket
 *
 * Creates a new UDP socket for sending/receiving datagrams.
 *
 * @return Socket file descriptor, or -1 on error
 */
int br_udp_socket_create(void)
{
    return socket(AF_INET, SOCK_DGRAM, 0);
}

/*======================================================================*
 * Socket Binding                                                     *
 *======================================================================*/

/**
 * @brief Bind UDP socket to a port
 *
 * Binds the socket to listen on the specified port.
 *
 * @param fd Socket file descriptor
 * @param port Port number
 * @return 0 on success, -1 on error
 */
int br_udp_bind(int fd, uint16_t port)
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    return bind(fd, (struct sockaddr*)&addr, sizeof(addr));
}

/*======================================================================*
 * Socket Options                                                   *
 *======================================================================*/

/**
 * @brief Set socket to non-blocking mode
 *
 * Sets the O_NONBLOCK flag on the socket for non-blocking I/O.
 *
 * @param fd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int br_udp_set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/*======================================================================*
 * Sending                                                          *
 *======================================================================*/

/**
 * @brief Send data to a remote host
 *
 * Sends a datagram to the specified host and port.
 *
 * @param fd Socket file descriptor
 * @param buf Data buffer
 * @param len Data length
 * @param host Remote hostname/IP
 * @param port Remote port
 * @return Bytes sent, or -1 on error
 */
ssize_t br_udp_sendto(int fd, const void* buf, size_t len,
                     const char* host, uint16_t port)
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    return sendto(fd, buf, len, 0,
                (struct sockaddr*)&addr, sizeof(addr));
}

/*======================================================================*
 * Receiving                                                        *
 *======================================================================*/

/**
 * @brief Receive data from a remote host
 *
 * Receives a datagram and stores the sender address.
 *
 * @param fd Socket file descriptor
 * @param buf Data buffer
 * @param len Buffer length
 * @param from Sender address (output)
 * @return Bytes received, or -1 on error
 */
ssize_t br_udp_recvfrom(int fd, void* buf, size_t len,
                       struct sockaddr_in* from)
{
    socklen_t fromlen = sizeof(*from);
    return recvfrom(fd, buf, len, 0,
                    (struct sockaddr*)from, &fromlen);
}