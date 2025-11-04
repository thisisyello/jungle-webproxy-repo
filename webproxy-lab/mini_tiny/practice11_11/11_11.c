#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <errno.h>
#include <string.h>
#include <strings.h>

static int read_line(int fd, char *buf, size_t max) {
    size_t i = 0;
    while (i + 1 < max) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if(errno == EINTR) {
                continue;
            }
            return -1;
        }
        buf[i++] = c;
        if (c =='\n') {
            break;
        }
    }
    buf[i] = '\0';
    return (int)i;
}

static int skip_headers(int fd) {
    char line[2048];
    int n;

    while ((n = read_line(fd, line, sizeof(line))) > 0) {
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) {
            return 0;
        }
    }
    return (n >= 0) ? 0 : -1;
}

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    int reuseaddr = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (reuseaddr < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        exit(1);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);
    int bind_return = bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (bind_return < 0) {
        perror("bind");
        exit(1);
    }

    int listen_return = listen(server_fd, 1024);
    if (listen_return < 0) {
        perror("listen");
        exit(1);
    }

    while (1) {
        struct sockaddr_in c_addr = {0};
        socklen_t c_len = sizeof(c_addr);
        int accept_fd = accept(server_fd, (struct sockaddr *)&c_addr, &c_len);
        if (accept_fd < 0) {
            perror("accept");
            continue;
        }

        // GET /index.html HTTP/1.1
        // reqline - 요청라인, method - GET, path - /index.html, vesion - 1.1
        char request_line[2048], method[32], path[1024], version[64];
        int get_line = read_line(accept_fd, request_line, sizeof(request_line));
        if (get_line < 0) {
            close(accept_fd);
            continue;
        }

        method[0] = path[0] = version[0] = '\0';
        sscanf(request_line, "%31s %1023s %63s", method, path, version);

        if (skip_headers(accept_fd) < 0) { 
            close(accept_fd);
            continue;
        }

        int is_head = 0;
        if (strcasecmp(method, "HEAD") == 0) {
            is_head = 1;
        } else if (strcasecmp(method, "GET") != 0) {
            const char *msg = "HTTP/1.0 501 Not Implemented\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Content-Length: 19\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Not Implemented.\n";
            send(accept_fd, msg, strlen(msg), 0);
            close(accept_fd);
            continue;
        }

        int open_fd = open("index.html", O_RDONLY);
        if (open_fd < 0) {
            perror("open");
            close(accept_fd);
            continue;
        }

        struct stat stat_buffer;
        int fstat_return = fstat(open_fd, &stat_buffer);
        if (fstat_return < 0) {
            perror("fstat");
            close(open_fd);
            close(accept_fd);
            continue;
        }

        char response_header[] = "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n"
            "\r\n";
        char header_buffer[512];
        int header_len = snprintf(
            header_buffer,
            sizeof(header_buffer),
            response_header,
            (long)stat_buffer.st_size
        );
        if (header_len < 0 || header_len >= (int)sizeof(header_buffer)) {
            fprintf(stderr, "header_len");
            close(open_fd);
            close(accept_fd);
            continue;
        }

        ssize_t off = 0;
        while (off < header_len) {
            ssize_t send_return = send(accept_fd,header_buffer + off, header_len - off, 0);
            if (send_return <= 0) {
                perror("send header");
                close(open_fd);
                close(accept_fd);
                break;
            }
            off += send_return;
        }

        if (!is_head) {
            char buf[8192];
            while(1) {
                ssize_t read_return = read(open_fd, buf, sizeof(buf));
                if (read_return < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("read file");
                    break;
                }
                if (read_return == 0) {
                    break;
                }
                ssize_t send_byte = 0;
                while (send_byte < read_return) {
                    ssize_t send_return = send(accept_fd, buf + send_byte, read_return - send_byte, 0);
                    if (send_return <= 0) {
                        perror("send body");
                        close(open_fd);
                        close(accept_fd);
                    }
                    send_byte += send_return;
                }
            }
        }
    }
    // close(server_fd)
    // return 0;
}