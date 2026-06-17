#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <errno.h>

typedef ssize_t (*recv_t)(int, void *, size_t, int);
typedef ssize_t (*recvfrom_t)(int, void *, size_t, int, struct sockaddr *, socklen_t *);

/*
 * process_netlink_buf - Intercepts and processes a Netlink message buffer.
 *
 * Rationale:
 * Mudfish-real queries routing tables via Netlink sockets. However, if the system
 * has policy routing tables (e.g., table 252 virtual routes from Tailscale/Wireguard),
 * Mudfish's fixed-size internal route list can overflow or crash.
 *
 * This function filters out all RTM_NEWROUTE messages that do not belong to the
 * main (254), local (255), or default (0) routing tables.
 *
 * Alignment:
 * Netlink messages are strictly aligned to 4-byte boundaries (NLMSG_ALIGNTO).
 * We use NLMSG_ALIGN(nlh->nlmsg_len) to correctly calculate the offset of the
 * next message and to copy the full aligned message (including padding bytes),
 * preventing data stream misalignment which previously caused parse failures
 * (MUDEC_01530: error in received packet).
 */
static ssize_t process_netlink_buf(int sockfd, void *buf, ssize_t n) {
    int saved_errno = errno;

    int domain = 0;
    socklen_t domain_len = sizeof(domain);
    int protocol = 0;
    socklen_t protocol_len = sizeof(protocol);

    /* Verify if the file descriptor is a Netlink routing socket */
    if (getsockopt(sockfd, SOL_SOCKET, SO_DOMAIN, &domain, &domain_len) == 0 && domain == AF_NETLINK &&
        getsockopt(sockfd, SOL_SOCKET, SO_PROTOCOL, &protocol, &protocol_len) == 0 && (protocol == NETLINK_ROUTE || protocol == 0)) {
        
        char *src = (char *)buf;
        char *dst = (char *)buf;
        ssize_t remaining = n;
        ssize_t new_len = 0;

        while (remaining >= (ssize_t)sizeof(struct nlmsghdr)) {
            struct nlmsghdr *nlh = (struct nlmsghdr *)src;
            /* Boundary check to avoid reading out-of-bounds */
            if (nlh->nlmsg_len < sizeof(struct nlmsghdr) || nlh->nlmsg_len > (__u32)remaining) {
                break; 
            }

            /* Calculate aligned length representing the message block size in the stream */
            size_t aligned_len = NLMSG_ALIGN(nlh->nlmsg_len);
            size_t copy_len = aligned_len;
            if (copy_len > (size_t)remaining) {
                copy_len = remaining;
            }

            int keep = 1;
            if (nlh->nlmsg_type == RTM_NEWROUTE) {
                if (nlh->nlmsg_len >= sizeof(struct nlmsghdr) + sizeof(struct rtmsg)) {
                    struct rtmsg *rtm = (struct rtmsg *)((char *)nlh + sizeof(struct nlmsghdr));
                    /* Only keep routes in main (254) and local (255) tables */
                    if (rtm->rtm_table != 254 && rtm->rtm_table != 255 && rtm->rtm_table != 0) {
                        keep = 0;
                    }
                }
            }

            /* Rebuild the buffer incrementally, keeping only approved routes */
            if (keep) {
                if (dst != src) {
                    memmove(dst, src, copy_len);
                }
                dst += copy_len;
                new_len += copy_len;
            }

            src += copy_len;
            remaining -= copy_len;
        }
        
        errno = saved_errno;
        return new_len;
    }

    errno = saved_errno;
    return n;
}

/*
 * recv - Hook for the recv() system call.
 * 
 * Rationale for Retry Loop:
 * If a packet chunk read from the Netlink socket contains only filtered-out routes
 * (e.g. all routes in the chunk belong to table 252), process_netlink_buf returns 0.
 * Returning 0 directly would be interpreted by the caller as EOF (socket closed by peer),
 * causing Mudfish to abort the dump prematurely with "odr_GetIpForwardInfo() failed".
 * Instead of returning 0, we loop and perform a new read to get the next chunk from the
 * kernel socket. The loop terminates when we either get a chunk containing at least
 * one kept route, or a genuine socket closure/error (n <= 0) occurs.
 */
ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    static recv_t real_recv = NULL;
    if (!real_recv) {
        real_recv = (recv_t)dlsym(RTLD_NEXT, "recv");
    }

    while (1) {
        ssize_t n = real_recv(sockfd, buf, len, flags);
        if (n <= 0) {
            return n;
        }

        ssize_t new_len = process_netlink_buf(sockfd, buf, n);
        if (new_len > 0) {
            return new_len;
        }
    }
}

/*
 * recvfrom - Hook for the recvfrom() system call.
 * 
 * Intercepted similarly to recv() since routing libraries (such as libnl or custom
 * clients inside mudfish-real) typically query routing sockets using recvfrom() to capture
 * peer identity.
 */
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    static recvfrom_t real_recvfrom = NULL;
    if (!real_recvfrom) {
        real_recvfrom = (recvfrom_t)dlsym(RTLD_NEXT, "recvfrom");
    }

    while (1) {
        ssize_t n = real_recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
        if (n <= 0) {
            return n;
        }

        ssize_t new_len = process_netlink_buf(sockfd, buf, n);
        if (new_len > 0) {
            return new_len;
        }
    }
}
