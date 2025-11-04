#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <errno.h>
#include <string.h>
#include <signal.h>

static ssize_t write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return (ssize_t)off;
}

static int read_line(int fd, char *buf, size_t max) {
    size_t i = 0;
    while (i + 1 < max) {
        char c;
        ssize_t n =read(fd, &c, 1);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (int)i;
}

static void skip_header(int fd) {
    char line[2048];
    while(1) {
        int n = read_line(fd, line, sizeof(line));
        if (n <= 0) break;
        if (!strcmp(line, "\r\n") || !strcmp(line, "\n")) {
            break;
        }
    }
}

static void send_404(int fd) {
    const char *m = "HTTP/1.0 404 Not Found\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Connection: close\r\n\r\nNot Found\n";
    // write(fd, m, strlen(m));
    if (write_all(fd, m, strlen(m)) < 0) {
        perror("write_all");
    }
}

static void send_500(int fd) {
    const char *m = "HTTP/1.0 500 Internal Server Error\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Connection: close\r\n\r\nInternal Error\n";
    // write(fd, m, strlen(m));
    if (write_all(fd, m, strlen(m)) < 0) {
        perror("write_all");
    }
}

static void serve_static(int client_fd, const char *filepath) {
    int open_fd = open(filepath, O_RDONLY);
    if (open_fd < 0) {
        send_404(client_fd);
        return;
    }

    struct stat st;
    if (fstat(open_fd, &st) < 0) {
        close(open_fd);
        send_500(client_fd);
        return;
    }

    char header[256];
    int n = snprintf(
        header,
        sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        (long)st.st_size
    );
    if (n > 0) {
        // write(client_fd, header, n);
        if (write_all(client_fd, header, (size_t)n) < 0) {
            perror("write_all");
        }
    }

    char buf[8192];
    while(1) {
        ssize_t read_return = read(open_fd, buf, sizeof(buf));
        if (read_return == 0) break;
        if (read_return < 0) break;
        ssize_t off = 0;
        while (off < read_return) {
            ssize_t write_return = write(client_fd, buf + off, read_return - off);
            if (write_return <= 0) {
                close(open_fd);
                return;
            }
            off += write_return;
        }
    }
    close(open_fd);
}

static void serve_cgi_adder(int client_fd, const char *query) {
    const char *pre = "HTTP/1.0 200 OK\r\n"
        "Connection: close\r\n";
    // write(client_fd, pre, strlen(pre));
    if (write_all(client_fd, pre, strlen(pre)) < 0) {
        perror("write_all");
    }

    pid_t pid = fork();
    if (pid < 0) {
        send_500(client_fd);
        return;
    }
    if (pid == 0) {
        setenv("REQUEST_METHOD", "GET", 1);
        setenv("QUERY_STRING", query ? query : "", 1);

        dup2(client_fd, STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
        }

        execl("../tiny/cgi-bin/adder", "../tiny/cgi-bin/adder", (char*)NULL);
        dprintf(STDOUT_FILENO, "Content-type: text/plain\r\n\r\nexec failed\n");
        _exit(1);
    } else {
        // waitpid(pid, NULL, 0);
    }
}

int main(void) {
    signal(SIGPIPE,SIG_IGN);

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

        char line[2048] = {0};
        if (read_line(accept_fd, line, sizeof(line)) <= 0) {
            close(accept_fd);
            continue;
        }

        char method[16], url[1024], version[16];
        if (sscanf(line, "%15s %1023s %15s", method, url, version) != 3) {
            send_500(accept_fd);
            close(accept_fd);
            continue;
        }
        skip_header(accept_fd);

        char *qmark = strchr(url, '?');
        char *query = NULL;
        if (qmark) {
            *qmark = '\0';
            query = qmark + 1;
        }

        if (strncmp(url, "/cgi-bin/adder", 14) == 0) {
            serve_cgi_adder(accept_fd, query);
            close(accept_fd);
            continue;
        }

        const char *path = (!strcmp(url, "/")) ? "index.html" : url + (url[0] =='/' ? 1 : 0);

        serve_static(accept_fd, path);
        close(accept_fd);
    }

    // close(server_fd)
    // return 0;
}