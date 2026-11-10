/*
 * wlproxy.c - minimal Wayland socket proxy that keeps the host cursor visible.
 *
 * Many apps (notably Sober/SDL) hide the desktop cursor with
 *   wl_pointer.set_cursor(serial, NULL, 0, 0)
 * and never ask the compositor for a cursor shape of their own. The result is
 * an invisible cursor over the window.
 *
 * This proxy sits between the app and the compositor, parses only the
 * client -> server direction, and drops those "hide cursor" requests. All
 * other messages are forwarded verbatim, including SCM_RIGHTS file
 * descriptors.
 *
 * The app connects to WAYLAND_DISPLAY=<proxy name>; the proxy connects to the
 * real socket named by SOBER_WL_REAL.
 *
 * Environment:
 *   WAYLAND_DISPLAY   socket name to listen on (required)
 *   SOBER_WL_REAL     real compositor socket name (required)
 *   WLPROXY_LOG       log file path (default: stderr)
 *   WLPROXY_DEBUG     if set, log registry binds and message sizes
 *
 * Build: make static   (or: cc -O2 -pthread -o wlproxy src/wlproxy.c)
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>

#define WLPROXY_VERSION "1.0.0"

#define CHUNK 65536
#define PARSE_MAX (1024 * 1024)
#define MAX_FDS 16

static int logfd = STDERR_FILENO;
static int g_debug;
static pthread_mutex_t logmu = PTHREAD_MUTEX_INITIALIZER;

static void logmsg(const char *fmt, ...)
{
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "[wlproxy %d] ", (int)getpid());
    if (n < 0)
        return;

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
    va_end(ap);
    if (m < 0)
        return;

    size_t len = (size_t)n + (size_t)m;
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    buf[len++] = '\n';

    pthread_mutex_lock(&logmu);
    if (logfd >= 0)
        (void)!write(logfd, buf, len);
    pthread_mutex_unlock(&logmu);
}

enum { OBJ_NONE = 0, OBJ_REGISTRY, OBJ_SEAT, OBJ_POINTER };

struct obj {
    uint32_t id;
    int type;
};

struct objmap {
    struct obj v[512];
    int n;
    int msg_count;
    int drop_count;
};

static int map_get(struct objmap *m, uint32_t id)
{
    for (int i = 0; i < m->n; i++)
        if (m->v[i].id == id)
            return m->v[i].type;
    return OBJ_NONE;
}

static void map_set(struct objmap *m, uint32_t id, int type)
{
    for (int i = 0; i < m->n; i++) {
        if (m->v[i].id == id) {
            m->v[i].type = type;
            return;
        }
    }
    if (m->n < (int)(sizeof(m->v) / sizeof(m->v[0]))) {
        m->v[m->n].id = id;
        m->v[m->n].type = type;
        m->n++;
    }
}

static void map_del(struct objmap *m, uint32_t id)
{
    for (int i = 0; i < m->n; i++) {
        if (m->v[i].id == id) {
            m->v[i] = m->v[--m->n];
            return;
        }
    }
}

static uint32_t rd32(const unsigned char *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/*
 * Parse complete messages in buf[0..len). Messages that should be forwarded
 * are copied into out (which has room for len bytes). Returns the number of
 * trailing bytes that form an incomplete message; the caller keeps them for
 * the next read. *dropped receives the number of dropped requests.
 */
static size_t parse_client(struct objmap *m, const unsigned char *buf, size_t len,
                           unsigned char *out, size_t *outlen, int *dropped)
{
    size_t off = 0;
    *outlen = 0;
    *dropped = 0;

    while (off + 8 <= len) {
        uint32_t id = rd32(buf + off);
        uint32_t w1 = rd32(buf + off + 4);
        uint32_t size = w1 >> 16;
        uint32_t opcode = w1 & 0xffff;

        if (size < 8 || size > PARSE_MAX) {
            /* Malformed stream; forward the rest untouched. */
            memcpy(out + *outlen, buf + off, len - off);
            *outlen += len - off;
            off = len;
            break;
        }
        if (off + size > len)
            break; /* incomplete message */

        int drop = 0;

        if (g_debug && m->msg_count < 40) {
            m->msg_count++;
            logmsg("msg id=%u op=%u size=%u", id, opcode, size);
        }

        if (id == 1) {
            /* wl_display.get_registry */
            if (opcode == 1 && size >= 12) {
                uint32_t nid = rd32(buf + off + 8);
                map_set(m, nid, OBJ_REGISTRY);
            }
        } else {
            int type = map_get(m, id);
            if (type == OBJ_REGISTRY) {
                /* wl_registry.bind(name, interface, version, new_id) */
                if (opcode == 0 && size >= 20) {
                    uint32_t slen = rd32(buf + off + 12);
                    if (slen > 0 && slen <= size - 16) {
                        const char *iface = (const char *)(buf + off + 16);
                        size_t pad = (slen + 3u) & ~3u;
                        size_t nid_off = off + 16 + pad + 4;
                        if (nid_off + 4 <= off + size) {
                            uint32_t nid = rd32(buf + nid_off);
                            uint32_t name = rd32(buf + off + 8);
                            uint32_t ver = rd32(buf + off + 16 + pad);
                            if (g_debug)
                                logmsg("registry.bind name=%u iface=%s ver=%u nid=%u",
                                       name, iface, ver, nid);
                            if (strcmp(iface, "wl_seat") == 0) {
                                map_set(m, nid, OBJ_SEAT);
                            } else if (strcmp(iface, "wl_pointer") == 0) {
                                map_set(m, nid, OBJ_POINTER);
                            } else {
                                map_del(m, nid);
                            }
                        }
                    }
                } else if (opcode == 1) {
                    map_del(m, id); /* wl_registry.destroy */
                }
            } else if (type == OBJ_SEAT) {
                /* wl_seat.get_pointer(new_id) */
                if (opcode == 0 && size >= 12) {
                    uint32_t nid = rd32(buf + off + 8);
                    map_set(m, nid, OBJ_POINTER);
                    if (g_debug)
                        logmsg("seat.get_pointer id=%u", nid);
                } else if (opcode == 3) {
                    map_del(m, id); /* wl_seat.release */
                }
            } else if (type == OBJ_POINTER) {
                /* wl_pointer.set_cursor(serial, surface, hotspot_x, hotspot_y) */
                if (opcode == 0 && size >= 24) {
                    uint32_t serial = rd32(buf + off + 8);
                    uint32_t surface = rd32(buf + off + 12);
                    if (surface == 0) {
                        drop = 1;
                        (*dropped)++;
                        m->drop_count++;
                        if (m->drop_count <= 10 || (m->drop_count % 200) == 0)
                            logmsg("drop wl_pointer.set_cursor(NULL) serial=%u id=%u (total %d)",
                                   serial, id, m->drop_count);
                    }
                } else if (opcode == 1) {
                    map_del(m, id); /* wl_pointer.release */
                }
            }
        }

        if (!drop) {
            memcpy(out + *outlen, buf + off, size);
            *outlen += size;
        }
        off += size;
    }

    return len - off;
}

static int send_with_fds(int fd, const void *buf, size_t len, int *fds, int nfds)
{
    size_t off = 0;
    while (off < len) {
        struct iovec iov;
        iov.iov_base = (void *)((const char *)buf + off);
        iov.iov_len = len - off;

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        char cbuf[CMSG_SPACE(sizeof(int) * MAX_FDS)];
        if (nfds > 0) {
            memset(cbuf, 0, sizeof(cbuf));
            msg.msg_control = cbuf;
            msg.msg_controllen = CMSG_SPACE(sizeof(int) * nfds);
            struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type = SCM_RIGHTS;
            c->cmsg_len = CMSG_LEN(sizeof(int) * nfds);
            memcpy(CMSG_DATA(c), fds, sizeof(int) * nfds);
        }

        ssize_t n = sendmsg(fd, &msg, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        off += (size_t)n;
        nfds = 0;
    }
    return 0;
}

static void *watchdog(void *p)
{
    pid_t ppid = *(pid_t *)p;
    for (;;) {
        sleep(1);
        if (getppid() != ppid) {
            logmsg("parent exited, shutting down");
            _exit(0);
        }
    }
    return NULL;
}

struct thread_arg {
    int from;
    int to;
    int parse;
};

static void *pump(void *p)
{
    struct thread_arg *a = p;
    unsigned char *out = malloc(CHUNK + PARSE_MAX);
    unsigned char *pend = malloc(PARSE_MAX);
    size_t pend_len = 0;
    int pending_fds[MAX_FDS];
    int npending = 0;
    struct objmap map;
    memset(&map, 0, sizeof(map));

    if (!out || !pend) {
        logmsg("alloc failed");
        goto done;
    }

    for (;;) {
        unsigned char rbuf[CHUNK];
        struct iovec iov;
        iov.iov_base = rbuf;
        iov.iov_len = sizeof(rbuf);

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        char cbuf[CMSG_SPACE(sizeof(int) * MAX_FDS)];
        msg.msg_control = cbuf;
        msg.msg_controllen = sizeof(cbuf);

        ssize_t n = recvmsg(a->from, &msg, MSG_CMSG_CLOEXEC);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;

        int fds[MAX_FDS];
        int nfds = 0;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                size_t cnt = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                for (size_t i = 0; i < cnt && nfds < MAX_FDS; i++) {
                    int fd;
                    memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
                    fds[nfds++] = fd;
                }
            }
        }

        if (!a->parse) {
            if (send_with_fds(a->to, rbuf, (size_t)n, fds, nfds) < 0)
                break;
            continue;
        }

        /* client -> server: append to the pending parse buffer */
        if (pend_len + (size_t)n > PARSE_MAX) {
            logmsg("parse buffer overflow, flushing raw");
            if (pend_len > 0 && send_with_fds(a->to, pend, pend_len, NULL, 0) < 0)
                break;
            if (send_with_fds(a->to, rbuf, (size_t)n, fds, nfds) < 0)
                break;
            pend_len = 0;
            continue;
        }
        memcpy(pend + pend_len, rbuf, (size_t)n);
        pend_len += (size_t)n;
        if (g_debug)
            logmsg("c2s recv %zd bytes (pend=%zu) fds=%d", n, pend_len, nfds);

        size_t outlen = 0;
        int dropped = 0;
        size_t leftover = parse_client(&map, pend, pend_len, out, &outlen, &dropped);

        if (outlen > 0) {
            int sendfds[MAX_FDS];
            int nsend = 0;
            for (int i = 0; i < npending && nsend < MAX_FDS; i++)
                sendfds[nsend++] = pending_fds[i];
            npending = 0;
            for (int i = 0; i < nfds && nsend < MAX_FDS; i++)
                sendfds[nsend++] = fds[i];
            if (send_with_fds(a->to, out, outlen, sendfds, nsend) < 0)
                break;
        } else {
            for (int i = 0; i < nfds && npending < MAX_FDS; i++)
                pending_fds[npending++] = fds[i];
        }

        if (leftover > 0)
            memmove(pend, pend + pend_len - leftover, leftover);
        pend_len = leftover;
    }

done:
    free(out);
    free(pend);
    shutdown(a->to, SHUT_WR);
    free(a);
    return NULL;
}

static int connect_unix(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    size_t plen = strlen(path);
    if (plen >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        close(fd);
        return -1;
    }
    memcpy(addr.sun_path, path, plen + 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void usage(const char *argv0)
{
    printf("wlproxy %s - drop wl_pointer.set_cursor(NULL) requests\n\n", WLPROXY_VERSION);
    printf("Usage: %s\n\n", argv0);
    printf("Environment:\n");
    printf("  WAYLAND_DISPLAY  socket name to listen on (required)\n");
    printf("  SOBER_WL_REAL    real compositor socket name (required)\n");
    printf("  WLPROXY_LOG      log file path (default: stderr)\n");
    printf("  WLPROXY_DEBUG    verbose logging when set\n");
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            printf("wlproxy %s\n", WLPROXY_VERSION);
            return 0;
        }
        fprintf(stderr, "wlproxy: unknown argument: %s\n", argv[1]);
        return 2;
    }

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *listen_name = getenv("WAYLAND_DISPLAY");
    const char *real_name = getenv("SOBER_WL_REAL");
    if (!runtime || !*runtime || !listen_name || !*listen_name ||
        !real_name || !*real_name) {
        fprintf(stderr,
                "wlproxy: XDG_RUNTIME_DIR, WAYLAND_DISPLAY and SOBER_WL_REAL must be set\n");
        return 1;
    }

    const char *logpath = getenv("WLPROXY_LOG");
    if (logpath && *logpath) {
        logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (logfd < 0)
            logfd = STDERR_FILENO;
    }
    g_debug = getenv("WLPROXY_DEBUG") != NULL;

    signal(SIGPIPE, SIG_IGN);

    char real_path[512], listen_path[512];
    snprintf(real_path, sizeof(real_path), "%s/%s", runtime, real_name);
    snprintf(listen_path, sizeof(listen_path), "%s/%s", runtime, listen_name);

    int lfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lfd < 0) {
        logmsg("socket: %s", strerror(errno));
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    size_t llen = strlen(listen_path);
    if (llen >= sizeof(addr.sun_path)) {
        logmsg("socket path too long: %s", listen_path);
        return 1;
    }
    memcpy(addr.sun_path, listen_path, llen + 1);

    unlink(listen_path);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        logmsg("bind %s: %s", listen_path, strerror(errno));
        return 1;
    }
    if (listen(lfd, 16) < 0) {
        logmsg("listen: %s", strerror(errno));
        return 1;
    }
    logmsg("listening on %s -> %s", listen_path, real_path);

    /* Exit when the parent app dies so the socket is released. */
    static pid_t parent_pid;
    parent_pid = getppid();
    pthread_t wd;
    if (pthread_create(&wd, NULL, watchdog, &parent_pid) == 0)
        pthread_detach(wd);

    for (;;) {
        int cfd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            logmsg("accept: %s", strerror(errno));
            break;
        }

        int sfd = connect_unix(real_path);
        if (sfd < 0) {
            logmsg("connect %s: %s", real_path, strerror(errno));
            close(cfd);
            continue;
        }
        if (g_debug)
            logmsg("connection accepted cfd=%d sfd=%d", cfd, sfd);

        struct thread_arg *a1 = malloc(sizeof(*a1));
        struct thread_arg *a2 = malloc(sizeof(*a2));
        if (!a1 || !a2) {
            free(a1);
            free(a2);
            close(cfd);
            close(sfd);
            continue;
        }
        a1->from = cfd;
        a1->to = sfd;
        a1->parse = 1;
        a2->from = sfd;
        a2->to = cfd;
        a2->parse = 0;

        pthread_t t1, t2;
        if (pthread_create(&t1, NULL, pump, a1) != 0) {
            free(a1);
            close(cfd);
            close(sfd);
            continue;
        }
        if (pthread_create(&t2, NULL, pump, a2) != 0) {
            free(a2);
            pthread_cancel(t1);
            pthread_join(t1, NULL);
            close(cfd);
            close(sfd);
            continue;
        }
        pthread_detach(t1);
        pthread_detach(t2);
    }

    return 0;
}
