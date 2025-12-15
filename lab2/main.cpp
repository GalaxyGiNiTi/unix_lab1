#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_hup = 0;

static void on_hup(int signo) {
    (void)signo;
    g_hup = 1;               // async-signal-safe
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

static int set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) return -1;
    return 0;
}

static void xwritef(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf) n = (int)sizeof buf;
    (void)write(STDOUT_FILENO, buf, (size_t)n); // write() безопаснее stdio в смысле простоты
}

static int make_listen_socket(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    if (set_cloexec(fd) < 0) { close(fd); return -1; }

    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        close(fd);
        return -1;
    }
    if (set_nonblock(fd) < 0) { // чтобы accept() можно было “дренировать” без блокировок
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv) {
    uint16_t port = 5555;
    if (argc >= 2) {
        long p = strtol(argv[1], NULL, 10);
        if (p <= 0 || p > 65535) {
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return 2;
        }
        port = (uint16_t)p;
    }

    // 1) Блокируем SIGHUP ДО установки обработчика и ДО входа в цикл.
    sigset_t block, origmask;
    sigemptyset(&block);
    sigaddset(&block, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &block, &origmask) < 0) {
        perror("sigprocmask(SIG_BLOCK)");
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_hup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // без SA_RESTART: pselect() корректно вернётся с EINTR
    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        perror("sigaction(SIGHUP)");
        return 1;
    }

    int lfd = make_listen_socket(port);
    if (lfd < 0) {
        perror("listen socket");
        return 1;
    }

    xwritef("Listening on port %u. PID=%ld (send: kill -HUP %ld)\n",
            (unsigned)port, (long)getpid(), (long)getpid());

    int cfd = -1; // единственное “оставленное” соединение

    for (;;) {
        // 2) Если сигнал уже получен (флаг выставлен), обрабатываем ДО ожидания.
        if (g_hup) {
            g_hup = 0;
            xwritef("[signal] SIGHUP received\n");
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        int maxfd = lfd;

        if (cfd >= 0) {
            FD_SET(cfd, &rfds);
            if (cfd > maxfd) maxfd = cfd;
        }

        // 3) Ключевой момент:
        //    pselect() атомарно подменяет маску на origmask (где SIGHUP НЕ заблокирован)
        //    и засыпает. После возврата маска восстановится обратно (SIGHUP снова заблокирован).
        int rc = pselect(maxfd + 1, &rfds, NULL, NULL, NULL, &origmask);

        if (rc < 0) {
            if (errno == EINTR) {
                // Разбудил сигнал — на следующей итерации увидим g_hup и выведем сообщение.
                continue;
            }
            perror("pselect");
            break;
        }

        // 4) Новые подключения: принимаем всё, но держим только одно.
        if (FD_ISSET(lfd, &rfds)) {
            for (;;) {
                struct sockaddr_in peer;
                socklen_t peerlen = sizeof peer;
                int nfd = accept(lfd, (struct sockaddr *)&peer, &peerlen);
                if (nfd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break; // всё приняли
                    if (errno == EINTR) continue;
                    perror("accept");
                    break;
                }

                (void)set_cloexec(nfd);
                (void)set_nonblock(nfd);

                char ip[INET_ADDRSTRLEN];
                const char *sip = inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
                unsigned p = (unsigned)ntohs(peer.sin_port);
                xwritef("[tcp] new connection fd=%d from %s:%u\n",
                        nfd, sip ? sip : "?", p);

                if (cfd < 0) {
                    cfd = nfd;
                    xwritef("[tcp] keeping fd=%d\n", cfd);
                } else {
                    xwritef("[tcp] closing extra fd=%d\n", nfd);
                    close(nfd);
                }
            }
        }

        // 5) Данные в “оставленном” соединении.
        if (cfd >= 0 && FD_ISSET(cfd, &rfds)) {
            char buf[4096];
            ssize_t n = read(cfd, buf, sizeof buf);
            if (n > 0) {
                xwritef("[tcp] received %zd bytes on fd=%d\n", n, cfd);
            } else if (n == 0) {
                xwritef("[tcp] peer closed fd=%d\n", cfd);
                close(cfd);
                cfd = -1;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    // просто попробуем позже
                } else {
                    perror("read");
                    close(cfd);
                    cfd = -1;
                }
            }
        }
    }

    if (cfd >= 0) close(cfd);
    close(lfd);
    return 0;
}
