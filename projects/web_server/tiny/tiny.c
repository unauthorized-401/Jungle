/*
  Tiny: 작은 웹서버 만들기
  GET 메서드를 사용하여 정적 및 동적 컨텐츠를 제공하는 HTTP/1.0 웹서버 구현
*/

#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
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

  // 소켓의 연결을 위해 Listen 소켓 Open
  listenfd = Open_listenfd(argv[1]);

  while (1) {
    clientlen = sizeof(clientaddr);

    // 반복적으로 Connect 요청
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // 트랜잭션 수행 후 Connect 소켓 Close
    doit(connfd);
    Close(connfd);
  }
}

// doit: 한 개의 트랜잭션을 수행하는 함수
void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  // Rio_readinitb(): Request 커맨드 라인 읽어들임
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  // GET 메서드만 지원하기 때문에 다른 헤더들은 무시

  // 숙제 11.11: HEAD 메서드 지원하도록 수정
  // HEAD 메서드는 GET 메서드와 동일하나, Response Body를 제외하고 Header만 전송한다.
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0)) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  // read_requesthdrs(): Request Header 정보 읽어들임
  read_requesthdrs(&rio);

  // 정적 컨텐츠 요청인지, 동적 컨텐츠 요청인지 체크
  is_static = parse_uri(uri, filename, cgiargs);

  // 요청 온 파일명이 유효하지 않다면,
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn’t find this file");
    return;
  }

  // 1) 정적 컨텐츠
  if (is_static) {
    // 정규 파일 및 읽기 권한(R)이 있는지,
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn’t read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);

  // 2) 동적 컨텐츠
  } else {
    // 정규 파일 및 실행 권한(X)이 있는지,
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn’t run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

// clienterror: 에러 발생 시 적절한 Status Code, Error Message를 출력하는 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  // Response Body 설정
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // 설정한 Response Body를 보냄
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));

  // Response Body에는 Contents의 Type, Length를 포함해야 함
  // 자식 프로세스는 모르는 내용이기 때문에, 부모 프로세스가 설정해주어야 함
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));

  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // Rio_writen(): write()을 사용하여 버퍼에 쓰는 함수
  Rio_writen(fd, body, strlen(body));
}

// read_requesthdrs: 헤더 정보를 읽는 함수
// 하지만 TINY는 헤더 정보를 필요로하지 않아, 마지막 라인일 때만 체크하여 리턴
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  // Rio_readlineb(): read()을 사용하여 버퍼 내용을 읽는 함수
  Rio_readlineb(rp, buf, MAXLINE);

  // Carriage return과 Line feed가 쌍으로 구성되어 있음
  // 1) Carriage return(CR): 커서를 맨 앞으로 이동시킴
  // 2) Line feed(LF): 커서를 아랫줄로 이동시킴
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }

  return;
}

// parse_uri: URI의 파라미터를 파싱하여, 정적 및 동적 컨텐츠를 구분하는 함수
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  // 1) 정적 컨텐츠
  // 홈 디렉토리 = 자신의 디렉토리 = /cgi-bin
  if (!strstr(uri, "cgi-bin")) {
    // 파라미터는 없음, URI는 상대경로로 변경
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);

    // URI가 '/'로 끝나면 Default 파일명(home.html)으로 설정
    if (uri[strlen(uri)-1] == '/') {
      strcat(filename, "home.html");
    }

    return 1;

  // 2) 동적 컨텐츠
  // URI가 cgi-bin을 포함한다면 동적 컨텐츠를 요청하는 것이라고 가정
  } else {
    // 파라미터 추출, 파라미터가 ? 다음으로 나열되어 오기 때문
    ptr = index(uri, '?');

    // 숙제 11.10: 파라미터가 있을 경우, 기존처럼 처리하고 없을 경우 adder.html 표시
    if (ptr) {
      // 파라미터가 있을 경우, 파라미터 설정해줌
      strcpy(cgiargs, ptr+1);
      // 문자열의 끝을 의미하는 '\0'을 넣음, Null 문자임
      *ptr = '\0';
      
      // URI를 상대경로로 변경
      strcpy(filename, ".");
      strcat(filename, uri);

      return 0;

    } else {
      // 파라미터가 없을 경우
      strcpy(cgiargs, "");
      
      // URI를 상대경로로 변경
      strcpy(filename, ".");
      strcat(filename, uri);
      strcat(filename, ".html");

      return 1;
    }
  }
}

// serve_static: 요청한 정적 데이터를 포함한 HTTP Response를 보내는 함수
// 정적 컨텐츠: HTML 파일, 무형식 파일, GIF, PNG, JPEG
void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // 확장자를 확인하여 파일 타입 결정
  get_filetype(filename, filetype);
  
  // Response header와 body를 설정 (빈 줄 한개가 헤더 종료를 뜻함)
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));

  printf("Response headers:\n");
  printf("%s", buf);

  // 숙제 11.11: HEAD 메서드면 Response Body 보내지 않음
  if (!strcasecmp(method, "HEAD")) {
    return;
  }

  // Response Body를 Client로 보냄, O_RDONLY: 읽기 전용으로 열기
  srcfd = Open(filename, O_RDONLY, 0);

  // 숙제 11.9를 위해 주석 처리
  /*
  // Mmap: srcfd 파일의 첫 번째 파일 크기 바이트를 주소 srcp에서 시작하는 가상 메모리의 읽기 전용 영역에 매핑
  // PROT_READ: 읽기 가능한 페이지, MAP_PRIVATE: 다른 프로세스와 대응 영역을 공유하지 않음(프로세스 내에서만 사용 가능)
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);

  // 식별자는 할일을 끝냈으므로 Close, 메모리 누수 가능성을 피함
  Close(srcfd);

  // Client에게 파일을 전송
  Rio_writen(fd, srcp, filesize);

  // 매핑된 가상 메모리 주소를 반환, 마찬가지로 메모리 누수 가능성을 피함
  Munmap(srcp, filesize);
  */

  // 숙제 11.9: mmap 대신 malloc 사용하도록 변경
  srcp = malloc(filesize);

  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);

  // malloc으로 할당해주었으니, free로 할당 해제
  free(srcp);
}

// serve_dynamic: 요청한 동적 데이터를 포함한 HTTP Response를 보내는 함수
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  // 처음엔 먼저 클라이언트에게 성공을 알리는 Response를 보냄
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // 숙제 11.11: HEAD 메서드면 Response Body 보내지 않음
  if (!strcasecmp(method, "HEAD")) {
    return;
  }

  // 자식 프로세스를 부모 프로세스를 '복제'하여 만드는 이유: README.md 파일 참고
  if (Fork() == 0) {
    // 자식 프로세스는 QUERY_STRING 변수를 요청 URI의 파라미터들로 초기화시킴
    // *실제 서버는 다른 환경 변수들도 추가적으로 설정
    setenv("QUERY_STRING", cgiargs, 1);

    // 자식 프로세스는 표준 출력을 Client와 연결된 연결 디스크립터로 리다이렉트함
    Dup2(fd, STDOUT_FILENO);

    // CGI 프로그램을 로드하고 실행
    Execve(filename, emptylist, environ);
  }

  // 부모 프로세스는 자식 프로세스가 종료될 때까지 대기
  Wait(NULL);
}

// get_filetype: 파일명의 확장자를 통해 Response Header에 넣을 설정값을 지정
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");

  } else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");

  } else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");

  } else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");

  // 숙제 11.7: MP4 파일을 처리하도록 수정, home.html도 mp4가 실행되도록 코드 수정
  } else if (strstr(filename, ".mp4")) {
    strcpy(filetype, "video/mp4");

  } else {
    strcpy(filetype, "text/plain");
  }
}