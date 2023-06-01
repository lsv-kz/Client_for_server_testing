#ifndef CLIENT_H
#define CLIENT_H

#define _FILE_OFFSET_BITS 64
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>

#include <mutex>
#include <thread>
#include <condition_variable>

#include <limits.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <poll.h>

extern char Host[128];
extern char IP[256];
extern int ai_family;
extern char Uri[1024];
extern int MaxThreads;
extern char Method[16];
extern int connKeepAlive;

extern const char *end_line;

const int  ERR_TRY_AGAIN = -1000;

const int  SIZE_BUF = 32768;

enum OPERATION_TYPE { CONNECT = 1, SEND_REQUEST, READ_RESP_HEADERS, READ_ENTITY, };

struct Config {
    int  num_connections;
    int  num_requests;

    char  Trigger;

    int  connKeepAlive;
    int  Timeout;
    int  TimeoutPoll;
    char ip[256];
    char port[32];
    const char *req;
    int (*create_sock)(const char*, const char*, int*);
};
extern const Config* const conf;
//----------------------------------------------------------------------
// 16284 32768 65536 
struct Connect {
    Connect *prev;
    Connect *next;

    OPERATION_TYPE operation;

    int    num_proc;
    int    num_conn;
    int    num_req;

    int    err;

    int    servSocket;

    time_t sock_timer;
    int    timeout;
    int    event;

    struct
    {
        const char *ptr;
        int len;
        int i;
    } req;

    struct
    {
        char  buf[SIZE_BUF];
        long  len;
        long  lenTail;
        char  *ptr;
        char  *p_newline;
    } resp;

    struct
    {
        int  chunk;
        long  size;
        int  end;
    } chunk;

    char   server[128];
    long long  cont_len;
    int    connKeepAlive;
    int    respStatus;
    long long  read_bytes;
};
//----------------------------------------------------------------------
int child_proc(int, const char*);
//----------------------------------------------------------------------
int get_good_req(void);
int get_good_conn(void);
void thr_client(int num_proc);
void push_to_wait_list(Connect *r);

int trig_get_good_req(void);
int trig_get_good_conn(void);
long long trig_get_all_read(void);
void thr_client_trigger(int num_proc);
void trig_push_to_wait_list(Connect *r);
//----------------------------------------------------------------------
int read_headers_to_stdout(Connect *resp);
int read_headers(Connect *req);
int read_to_space(int fd_in, char *buf, long size_buf, long *size, int timeout);
int read_to_space2(int fd_in, char *buf, long size, int timeout);
int read_timeout(int fd, char *buf, int len, int timeout);
int write_timeout(int fd, const char *buf, int len, int timeout);
void *get_request(void *arg);
int read_chunk(Connect *resp);
long long get_all_read(void);
int read_req_file(const char *path, char *req, int size);

int write_to_serv(Connect *req, const char *buf, int len);
int read_from_server(Connect *req, char *buf, int len);
int read_http_headers(Connect *r);
//----------------------------------------------------------------------
int create_client_socket(const char * host, const char *port);
int create_client_socket_ip4(const char *ip, const char *port, int*);
int create_client_socket_ip6(const char *ip, const char *port, int*);
int get_ip(int sock, char *ip, int size_ip);
const char *get_str_ai_family(int ai_family);
//----------------------------------------------------------------------
void std_in(char *s, int len);
const char *strstr_case(const char *s1, const char *s2);
int strcmp_case(const char *s1, const char *s2);
int strlcmp_case(const char *s1, const char *s2, int len);
int parse_headers(Connect *r, char *s);
int create_log_file();
const char *get_str_operation(OPERATION_TYPE n);
void hex_dump_stderr(const void *p, int n);
void hex_dump_stderr(const char *s, int line, const void *p, int n);

#endif
