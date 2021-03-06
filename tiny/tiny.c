/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* MAXLINE, MAXBUF == 8192 */

/* 인자 개수 argc 인자 값들 */
int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* Open_listenfd 함수를 호출해서 듣기 소켓을 오픈한다. */
  listenfd = Open_listenfd(argv[1]);

  /* 전형적인 무한 서버 루프를 실행 */
  while (1)
  {
    clientlen = sizeof(clientaddr);
    /* 반복적으로 연결 요청을 접수 */
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    /* 트랜젝션을 수행 */
    doit(connfd); // line:netp:tiny:doit
    /* 트랜잭션이 수행된 후 자신 쪽의 연결 끝 (소켓) 을 닫는다. */
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  /* Rio = Robust I/O */

  Rio_readinitb(&rio, fd);

  /* Rio_readlineb(그림 10.8)를 통해 요청 라인을 읽어들이고, 분석한다. 70 +4 line */
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  /* Tiny 는 GET method 만 지원하기에 클라이언트가 다른 메소드 (Post 같은)를 
   *요청하면 에러메세지를 보내고, main routin으로 돌아온다
   * 79 +4 line
   */
  if (strcasecmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  /* GET method라면 읽어들이고, 다른 요청 헤더들을 무시한다. */
  read_requesthdrs(&rio);

  /* Parse URI form GET request */
  /* URI 를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고, 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정한다.  */
  is_static = parse_uri(uri, filename, cgiargs);

  /* 만일 파일이 디스크상에 있지 않으면, 에러메세지를 즉시 클라아언트에게 보내고 메인 루틴으로 리턴*/
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  /* Serve static content */
  if (is_static)
  {
    /* 동적 컨텐츠이고 (위if), 이 파일이 보통 파일인지, 읽기 권한을 가지고 있는지 검증한다. */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }
    /* 그렇다면 정적 컨텐츠를 클라이언트한테 제공. */
    serve_static(fd, filename, sbuf.st_size);
  }
  else /* Serve dynamic content */
  {
    /* 실행 가능한 파일인지 검증*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }
    /* 그렇다면 동적 컨텐츠를 클라이언트에게 제공 */
    serve_dynamic(fd, filename, cgiargs);
  }
}

/* 명백한 오류에 대해서 클라이언트에게 보고하는 함수. 
 * HTTP응답을 응답 라인에 적절한 상태 코드와 상태 메시지와 함께 클라이언트에게 보낸다.
 * 
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* BUILD the HTTP response body */
  /* 브라우저 사용자에게 에러를 설명하는 응답 본체에 HTML파일도 함께 보낸다 */
  /* HTML 응답은 본체에서 컨텐츠의 크기와 타입을 나타내야하기에, HTMl 컨텐츠를 한 개의 스트링으로 만든다. */
  /* 이는 sprintf를 통해 body는 인자에 스택되어 하나의 긴 스트리잉 저장된다. */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  /* Rio_writen 그림 10.3 */
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* Tiny는 요청 헤더 내의 어떤 정보도 사용하지 않는다
 * 단순히 이들을 읽고 무시한다. 
 */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  printf("/*---------- reqeust hdrs 1 here! ----------*/\n");
  /* strcmp 두 문자열을 비교하는 함수 */
  /* 헤더의 마지막 줄은 비어있기에 \r\n   */
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  /* strstr */
  if (!strstr(uri, "cgi-bin")) /* Static content */
  {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);

    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "home.html");
    return 1;
  }

  else /* Dynamic content */
  {
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      /* ptr 초기화 */
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");

    strcpy(filename, ".");
    strcat(filename, uri);

    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF], *fbuf;

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

  /* writen = client 쪽에 */
  Rio_writen(fd, buf, strlen(buf));

  /* 서버 쪽에 출력 */
  printf("Response headers:\n");
  printf("%s", buf);

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // Close(srcfd);
  // Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);

  /* 숙제문제 11.9 */
  fbuf = malloc(filesize);
  Rio_readn(srcfd, fbuf, filesize);
  Close(srcfd);
  Rio_writen(fd, fbuf, filesize);
  free(fbuf);
}

/*
 * get_filetype - Derive file type from filename
 * strstr 두번쨰 인자가 첫번째 인자에 들어있는지 확인
 */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  /* 11.7 숙제 문제 - Tiny 가 MPG  비디오 파일을 처리하도록 하기.  */
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) /* Child process 생성 - 부모 프로세스(지금) 을 복사한*/
  {
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1);   // 환경변수 설정
    Dup2(fd, STDOUT_FILENO);              /* Redirect stdout to client, 자식 프로세스의 표준 출력을 연결 파일 식별자로 재지정 */
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child 부모 프로세스가 자식 프로세스가 종료될떄까지 대기하는 함수*/
}