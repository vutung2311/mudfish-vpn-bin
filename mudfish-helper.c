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

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    static recv_t real_recv = NULL;
    if (!real_recv) {
        real_recv = (recv_t)dlsym(RTLD_NEXT, "recv");
    }

    ssize_t n = real_recv(sockfd, buf, len, flags);
    if (n <= 0) {
        return n;
    }

    // Save errno to avoid getsockopt failures contaminating it
    int saved_errno = errno;

    // Check if it is a netlink route socket
    int domain = 0;
    socklen_t domain_len = sizeof(domain);
    int protocol = 0;
    socklen_t protocol_len = sizeof(protocol);

    if (getsockopt(sockfd, SOL_SOCKET, SO_DOMAIN, &domain, &domain_len) == 0 && domain == AF_NETLINK &&
        getsockopt(sockfd, SOL_SOCKET, SO_PROTOCOL, &protocol, &protocol_len) == 0 && (protocol == NETLINK_ROUTE || protocol == 0)) {
        
        char *src = (char *)buf;
        char *dst = (char *)buf;
        ssize_t remaining = n;
        ssize_t new_len = 0;

        while (remaining >= (ssize_t)sizeof(struct nlmsghdr)) {
            struct nlmsghdr *nlh = (struct nlmsghdr *)src;
            if (nlh->nlmsg_len < sizeof(struct nlmsghdr) || nlh->nlmsg_len > (__u32)remaining) {
                break; // invalid length
            }

            int keep = 1;
            if (nlh->nlmsg_type == RTM_NEWROUTE) {
                struct rtmsg *rtm = (struct rtmsg *)((char *)nlh + sizeof(struct nlmsghdr));
                // Only keep routes in main (254) and local (255) tables
                if (rtm->rtm_table != 254 && rtm->rtm_table != 255 && rtm->rtm_table != 0) {
                    keep = 0;
                }
            }

            if (keep) {
                if (dst != src) {
                    memmove(dst, src, nlh->nlmsg_len);
                }
                dst += nlh->nlmsg_len;
                new_len += nlh->nlmsg_len;
            }

            src += nlh->nlmsg_len;
            remaining -= nlh->nlmsg_len;
        }
        
        errno = saved_errno;
        return new_len;
    }

    errno = saved_errno;
    return n;
}

