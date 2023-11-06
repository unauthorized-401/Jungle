/*
    Web Proxy
    : Web browser와 End server 사이에서 중개자 역할을 하는 프로그램
    : 브라우저는 프록시에 연결, 프록시가 서버로 대신 연결하여 요청 전달
    : 서버가 프록시에 응답, 프록시가 브라우저로 응답 전달

    1) 방화벽: 브라우저가 프록시를 통해서만 방화벽 너머의 서버에 연결할 수 있음
    2) 익명화: 프록시는 브라우저의 모든 식별 정보를 제거하여 전달
    3) 캐시: 프록시는 서버 개체의 복사본을 저장, 다시 통신하지 않고 캐시에서 읽어 향후 요청에 응답

    Part 1
    : 들어온 연결 수락
    : 요청 분석 및 웹 서버로 전달
    : 응답 분석 및 클라이언트로 전달

    Part 2
    : 여러 동시 연결을 처리하도록

    Part 3
    : 프록시에 캐싱 추가
    : 최근에 액세스한 웹 콘텐츠의 간단한 메인 메모리 캐시 사용
*/

#include "csapp.h"

// 최대 캐시 사이즈 1 MiB, 메타 데이1터 등 불필요한 바이트는 무시
#define MAX_CACHE_SIZE 1049000

// 최대 객체 사이즈 100 KiB
#define MAX_OBJECT_SIZE 102400

// 최근 사용 횟수가 가장 적은 페이지를 삭제하는 방법인 LRU를 사용 (Least Recently Used)
#define LRU_PRIORITY 9999
#define MAX_OBJECT_SIZE_IN_CACHE 10

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void *thread(void *vargsp);
void doit(int connfd);
int check_cache(char *url, char *uri, int connfd);
void make_header(char *final_header, char *hostname, char *path, rio_t *client_rio);
int server_connection(char *hostname, int port);
void parse_uri(char *uri, char *hostname, int *port, char *path);

void cache_init();
int cache_find(char *url);
void cache_uri(char *uri, char *buf);

void read_before(int i);
void read_after(int i);

/*
    [Synchronization]
    : 캐시에 대한 접근은 thread-safe 해야 함
    : Pthreads readers-writers locks, Readers-Writers with semaphores 옵션 등이 있음
*/

typedef struct {
    char cache_object[MAX_OBJECT_SIZE];
    char cache_url[MAXLINE];

    // 우선순위가 높을 수록 오래된 페이지, 즉 수가 낮을 수록 삭제될 가능성이 높음
    int priority;
    int is_empty;

    int read_count;
    sem_t wmutex;
    sem_t rdcntmutex;
    
} cache_block;

typedef struct {
    cache_block cache_items[MAX_OBJECT_SIZE_IN_CACHE];
} Cache;

Cache cache;

int main(int argc, char **argv) {
    // 여러 개의 동시 연결을 처리하기 위해 스레드 사용
    pthread_t tid;

    cache_init(); 

    // listenfd와 connfd를 구분하는 이유
    // multi client가 요청할 때를 대비, 대기타는 스레드 따로 연결하는 스레드 따로 있어야 함
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    // 파라미터 개수가 항상 2개여야 하는 이유
    // 프로그램 자체가 첫번째 파라미터, 필요한 파라미터는 1개뿐이기 때문에 argc는 2여야 함
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /*
        한 Client가 종료되었을 때, 남은 다른 Client에 간섭가지 않도록 하기 위함

        더 자세히
        한 Client가 비정상적인 종료를 한 경우, Server는 이를 모르고 해당 Client로 Response를 보낼 수도 있음
        이때 잘못됐다는 Signal이 날라옴, Signal을 받으면 전체 프로세스 종료
        이 경우 연결되어 있는 다른 Client들도 종료될 수 있기 때문에 이 시그널을 무시하라는 의미
    */
    Signal(SIGPIPE, SIG_IGN);

    // 소켓의 연결을 위해 Listen 소켓 Open
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);

        // 반복적으로 Connect 요청
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        /*
            tid: 스레드 식별자, 스레드마다 식별 번호를 지정해주어야 함
            NULL: 스레드 option 지정, 기본이 NULL
            thread: 스레드가 해야 하는 일을 함수로 만들어, 그 함수명을 지정
            (void *)connfd: 스레드 함수의 매개변수를 지정, 즉 thread((void *)connfd)로 호출한 셈
        */
        Pthread_create(&tid, NULL, thread, (void *)connfd);
    }
}

/*
    멀티 스레드: 여러 개의 동시 요청 처리

    동시 서버를 구현하는 가장 간단한 방법은 새 스레드를 생성하는 것
    각 스레드가 새 연결 요청을 처리함

    1) 메모리 누수를 방지하려면 스레드를 분리 모드로 실행
    2) getaddrinfo 함수를 사용하면 thread safe함
*/
void *thread(void *vargsp) {
    int connfd = (int)vargsp;

    // 메인 스레드로부터 현재 스레드를 분리시킴
    // 분리시키는 이유: 해당 스레드가 종료되는 즉시 모든 자원을 반납할 것(free)을 보증, 분리하지 않으면 따로 pthread_join(pid)를 호출해야함
    Pthread_detach(Pthread_self());

    // 트랜잭션 수행 후 Connect 소켓 Close
    doit(connfd);
    Close(connfd);
}

// doit: 한 개의 트랜잭션을 수행하는 함수
void doit(int connfd) {
    int serverfd, port;
    char server_header[MAXLINE];
    char buf[MAXLINE], cachebuf[MAX_OBJECT_SIZE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], url[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    rio_t clientrio, serverrio;

    // Rio_readinitb(): clientrio와 connfd를 연결
    Rio_readinitb(&clientrio, connfd);
    Rio_readlineb(&clientrio, buf, MAXLINE);
    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method.");
        return;
    }

    // check_cache 함수를 이용해서 캐시에 등록되어 있는지 확인, 있으면 url에 저장함
    if (!(check_cache(url, uri, connfd))) {
        return;
    }
    
    // URI를 파싱하여 hostname, port, path를 얻고, 조건에 부합하는 헤더 생성
    parse_uri(uri, hostname, &port, path);
    make_header(server_header, hostname, path, &clientrio);

    // 서버와의 연결 (인라인 함수)
    serverfd = server_connection(hostname, port);

    // Rio_readinitb(): serverrio와 serverfd를 연결
    Rio_readinitb(&serverrio, serverfd);
    Rio_writen(serverfd, server_header, strlen(server_header));

    // 서버로부터 응답을 받고 클라이언트로 보내줌
    size_t response;
    int bufsize = 0;
    while ((response = Rio_readlineb(&serverrio, buf, MAXLINE)) != 0) {
        bufsize += response;

        // 최대 개체 사이즈보다 작으면, 받은 응답을 캐시에 저장
        if (bufsize < MAX_OBJECT_SIZE) {
            strcat(cachebuf, buf);
        }

        Rio_writen(connfd, buf, response);
    }

    Close(serverfd);

    if (bufsize < MAX_OBJECT_SIZE) {
        // URL에 cachebuf 저장
        cache_uri(url, cachebuf);
    }
}

int check_cache(char *url, char *uri, int connfd) {
    strcpy(url, uri);

    // cache_find 함수를 통해 search, -1이 아니라면 캐시에 저장되어 있다는 의미
    int index;

    if ((index = cache_find(url)) != -1) {
        // read_before 함수를 통해 캐시 뮤텍스를 열어줌
        read_before(index);

        // 캐시에서 찾은 값을 connfd에 쓰고, 바로 보냄
        Rio_writen(connfd, cache.cache_items[index].cache_object, strlen(cache.cache_items[index].cache_object));

        // read_after 함수를 통해 캐시 뮤텍스를 닫아줌
        read_after(index);

        return 0;
    }

    return 1;
}

/*
    Request Header는 아래 다섯가지 사항을 포함해야 함
    1) GET / HTTP/1.1
    2) Host: www.cmu.edu (호스트명)
    3) User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3
    4) Connection: close
    5) Proxy-Connection: close
*/
void make_header(char *final_header, char *hostname, char *path, rio_t *client_rio) {
    char buf[MAXLINE];
    char request_header[MAXLINE], host_header[MAXLINE], etc_header[MAXLINE];
    
    sprintf(request_header, "GET %s HTTP/1.0\r\n", path);
    
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        // 빈 줄이 들어오면 헤더가 끝났다는 뜻임으로,
        if (strcmp(buf, "\r\n")==0) {
            break;
        }

        if (!strncasecmp(buf, "Host", strlen("Host"))) {
            strcpy(host_header, buf);
            continue;
        }

        if (strncasecmp(buf, "User-Agent", strlen("User-Agent"))
         && strncasecmp(buf, "Connection", strlen("Connection"))
         && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection"))) {
            // 위 세가지 헤더 이외의 다른 헤더가 요청되었을 때, 따로 저장하여 전달
            strcat(etc_header, buf);
        }
    }

    if(strlen(host_header) == 0) {
        sprintf(host_header, "Host: %s\r\n", hostname);
    }

    sprintf(final_header, "%s%s%s%s%s%s%s",
            request_header,
            host_header,
            "Connection: close\r\n",
            "Proxy-Connection: close\r\n",
            user_agent_hdr,
            etc_header,
            "\r\n");
}

/*
    inline 함수: 호출 시 호출한 자리에서 인라인 함수 코드 자체가 안으로 들어감
    inline 함수로 선언한 이유: 서버와 커넥션하는 동안 다른 간섭을 받지 않기 위해
    
    + 성능 향상: 호출한 곳에 코드가 직접 쓰여진 것과 같은 효과, 함수 호출에 대한 오버헤드가 줄어듬
*/
// server_connection: 서버와 연결을 하기 위한 함수
inline int server_connection(char *hostname, int port){
    char portStr[100];

    // Open_clientfd 함수는 port를 문자 파라미터로 넣어야 함
    sprintf(portStr, "%d", port);

    return Open_clientfd(hostname, portStr);
}

/*
    [HTTP 요청 포트]
    포트가 주어지면 해당 포트로 요청,
    포트가 주어지지 않는다면 기본 포트 80 포트 사용
*/
// parse_uri: URI의 파라미터를 파싱하여, hostname, port, path를 얻는 함수
void parse_uri(char *uri, char *hostname, int *port, char *path) {
    char *first = strstr(uri, "//");

    // URI에 //이 포함되어 있으면 그 다음부터(first+2) 읽어들임
    first = first != NULL? first+2 : uri;

    char *next = strstr(first, ":");

    *port = 80;
    if (next) {
        *next = '\0';
        sscanf(first, "%s", hostname);

        // URI에 :이 포함되어 있으면 그 다음부터(next+1) 읽어들임
        sscanf(next+1, "%d%s", port, path);

    } else {
        next = strstr(first, "/");

        if(next) {
            *next = '\0';
            sscanf(first, "%s", hostname);

            *next = '/';
            sscanf(next, "%s", path);

        } else {
            sscanf(first, "%s", hostname);
        }
    }
}

void cache_init() {
  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++) {
    cache.cache_items[i].priority = 0;
    cache.cache_items[i].is_empty = 1;
    cache.cache_items[i].read_count = 0;

    /*
        첫번째: 초기화할 세마포어의 포인터 지정
        두번째: 스레드끼리 세마포어를 공유하려면 0, 프로세스끼리 공유하려면 다른 숫자로 설정
        세번째: 초기값 설정

        두번째와 관련,
        세마포어는 프로세스를 사용하기 때문에, 얘를 뮤텍스로 쓰고 싶을 때 두번째 파라미터를 0으로 설정하여 스레드끼리 공유하도록 함

        wmutex: 캐시에 접근하는 걸 방지
        rdcntmutex: 리드카운트에 접근하는 걸 방지
    */
    Sem_init(&cache.cache_items[i].wmutex, 0, 1);
    Sem_init(&cache.cache_items[i].rdcntmutex, 0, 1);
  }
}

/*
    P()
    : sem_wait(), 세마포어 값을 1 감소시킴
    : 0 이상이면 즉시 리턴, 음수가 되면 wait 상태가 되며 sem_post()가 호출될 때까지 wait함

    V()
    : sem_post(), 세마포어 값을 1 증가시킴
    : 이로 인해 값이 1 이상이 된다면 wait 중인 스레드 중 하나를 깨움
*/
void read_before(int index) {
    // P 함수를 통해 rdcntmutex에 접근 가능하게 해줌
    P(&cache.cache_items[index].rdcntmutex);

    // 사용하지 않는 애라면 0에서 1로 바뀌었을 것
    cache.cache_items[index].read_count += 1;

    // 1일 때만 캐쉬에 접근 가능, 누가 쓰고 있는 애라면 2가 되기 때문에 if문으로 들어올 수 없음
    if (cache.cache_items[index].read_count == 1) {
        // wmutex에 접근 가능하게 해줌, 즉 캐시에 접근
        P(&cache.cache_items[index].wmutex);
    }

    // V 함수를 통해 접근 불가능하게 해줌
    V(&cache.cache_items[index].rdcntmutex);
}

void read_after(int index) {
    P(&cache.cache_items[index].rdcntmutex);
    cache.cache_items[index].read_count -= 1;

    if (cache.cache_items[index].read_count == 0) {
        V(&cache.cache_items[index].wmutex);
    }

    V(&cache.cache_items[index].rdcntmutex);
}

int cache_find(char *url) {
    for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++) {
        read_before(i);

        // 캐시가 비어있고, 해당 url이 이미 캐시에 들어있을 경우
        if (cache.cache_items[i].is_empty == 0 && strcmp(url, cache.cache_items[i].cache_url) == 0) {
            read_after(i);
            return i;
        }

        read_after(i);
    }

    return -1;
}

// cache_eviction: 캐시에 공간이 필요하여 데이터를 지우는 함수
int cache_eviction() {
    int min = LRU_PRIORITY;
    int index = 0;

    for (int i=0; i < MAX_OBJECT_SIZE_IN_CACHE; i++) {
        read_before(i);

        if (cache.cache_items[i].is_empty == 1) {
            index = i;
            read_after(i);
            break;
        }

        if (cache.cache_items[i].priority < min) {
            index = i;
            min = cache.cache_items[i].priority;
            read_after(i);
            continue;
        }

        read_after(i);
    }

    return index;
}

// cache_LRU: 현재 캐시의 우선순위를 모두 올림, 최근 캐시 들어갔으므로
void cache_LRU(int index) {
    for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++) {
        if (i == index) {
            continue;
        }

        P(&cache.cache_items[i].wmutex);

        if (cache.cache_items[i].is_empty == 0) {
            cache.cache_items[i].priority -= 1;
        }

        V(&cache.cache_items[i].wmutex);
    }
}

// cache_uri: 빈 캐시를 찾아 값을 넣어주고 나머지 캐시의 우선순위를 내려주는 함수
void cache_uri(char *uri, char *buf) {
    // cache_eviction 함수를 이용하여 빈 캐시 블럭을 찾음
    int index = cache_eviction();

    P(&cache.cache_items[index].wmutex);

    strcpy(cache.cache_items[index].cache_object, buf);
    strcpy(cache.cache_items[index].cache_url, uri);

    // 방금 채웠으니, 우선순위는 9999로
    cache.cache_items[index].is_empty = 0;
    cache.cache_items[index].priority = 9999;

    // 방금 채웠으니, 나머지 캐시의 우선순위를 모두 올려야 함
    cache_LRU(index);

    V(&cache.cache_items[index].wmutex);
}