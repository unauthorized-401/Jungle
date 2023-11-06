/*
 * adder.c
 * 두 개의 수를 더하여 결과를 알려주는 CGI 프로그램
 */
#include "csapp.h"

int main(void) {
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1=0, n2=0;

  // 숙제 11.10: 두 파라미터를 파싱하여 저장하기 위한 변수
  char *arg1_ptr, *arg2_ptr;
  char value1[MAXLINE], value2[MAXLINE];

  // QUERY_STRING으로부터 두 파라미터 값을 얻음
  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&');
    *p = '\0';

    strcpy(arg1, buf);
    strcpy(arg2, p+1);

    if (strstr(buf, "first") != NULL) {
      // 숙제 11.10: HTML에서 사용자로부터 두 개의 수를 받아 처리하도록 변경

      // 1) first=value1에서 value1만 추출
      arg1_ptr = strchr(arg1, '=');
      *arg1_ptr = '\0';
      strcpy(value1, arg1_ptr + 1);

      // 1) second=value2에서 value2만 추출
      arg2_ptr = strchr(arg2, '=');
      *arg2_ptr = '\0';
      strcpy(value2, arg2_ptr + 1);

      n1 = atoi(value1);
      n2 = atoi(value2);
      
    } else {

      n1 = atoi(arg1);
      n2 = atoi(arg2);
    }
  }

  // 1) Response Body 생성
  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  // 2) HTTP Response 생성
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  printf("%s", content);
  fflush(stdout);

  exit(0);
}