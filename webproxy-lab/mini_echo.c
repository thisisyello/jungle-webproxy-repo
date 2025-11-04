// #include <arpa/inet.h>
// #include <errno.h>
// #include <stdint.h>
#include <sys/socket.h> // socket, bind, listen, accept
#include <netinet/in.h> // INADDR_ANY, htonl, htons
#include <unistd.h> // read, write
#include <stdlib.h> // exit
#include <stdio.h> // perror

int main(void) {
    // 해윙!
    printf("hello, echo!\n");

    // int socket(int domain, int type, int protocol);
    // domain - AF_INET(IPv4) or AF_INET6(IPv6) 등
    // type - SOCK_STREAM(TCP) or SOCK_DGRAM(UDP) 등
    // protocol - IPPROTO_TCP or IPPROTO_UDP or 0 등
    int ssocket = socket(AF_INET, SOCK_STREAM, 0);
    if (ssocket < 0) {
        perror("socket");
        exit(1);
    }

    // 필수는 아니지만요 bind TIME_WAIT 뭐시기 때문에 하는거요
    // int on = 1;
    // int ssetsockopt = setsockopt(ssocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    // if (ssetsockopt < 0) {
    //     perror("setsockopt");
    //     exit(1);
    // }

    struct sockaddr_in addr; // 서버 주소
    addr.sin_family = AF_INET; // 주소체계
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 이건 뭐누? 다 받아? IP 주소
    addr.sin_port = htons(8080); // 포트 번호

    // int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    // sockfd - 위에 socket에서 받은 fd
    // *addr - bind할 주소 정보를 담은 구조체 포인터
    // addlen - 주소 구조체의 크기
    int bbind = bind(ssocket, (struct sockaddr *)&addr, sizeof(addr));
    if (bbind < 0) {
        perror("bind");
        exit(1);
    }

    // int listen(int sockfd, int backlog);
    // sockfd - 위에 socket에서 받은 fd
    // backlog - 동시 대기 가능한 연결 요청 수 (1, 10, 128, 1024 등)
    int llisten = listen(ssocket, 1024);
    if (llisten < 0) {
        perror("listen");
        exit(1);
    }

    // 만약 위의 구조체를 그대로 사용하면 addr 구조체의 내용을 덮어씌움
    struct sockaddr_in c_addr; // 클라이언트 주소
    socklen_t len = sizeof(c_addr);

    // int accept(int listenfd, struct sockaddr *addr, int *addrlen);
    // listenfd - 위에 socket에서 받은 fd
    // *addr - bind할 주소 정보를 담은 구조체 포인터
    // *addlen - c_addr의 크기 !!포인터!! 포인터 포인터 포인텉ㅌ터터
    int aaccept = accept(ssocket, (struct sockaddr *)&c_addr, &len);
    if (aaccept < 0) {
        perror("accept");
        exit(1);
    }

    // 임시로 데이터 저장 1024바이트
    char buf[1024];
    // read(int fd, void *buf, size_t count)
    // fd - 위에 accept에서 받은 fd
    // *buf - read한 데이터를 저장할 주소 값
    // count - buf에 read할 최대 바이트 
    ssize_t rread = read(aaccept, buf, sizeof(buf));

    // write(int fd, const void *buf, size_t nbytes);
    // fd - 데이터를 쓸 fd
    // *buf - 파일에 쓸 데이터를 담고있는 버퍼
    // nbytes - buf에서 파일로 쓸 데이터 바이트
    write(aaccept, buf, rread);

    // 연결 끝났으니까 accept 종료 하고, socket도 닫아!
    close(aaccept);
    close(ssocket);

    return 0;
}