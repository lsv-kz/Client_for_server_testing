#include "client.h"

using namespace std;
//======================================================================
void get_time_connect(struct timeval *time1, char *buf, int size_buf)
{
    unsigned long ts12, tu12;
    struct timeval time2;

    gettimeofday(&time2, NULL);

    if ((time2.tv_usec-time1->tv_usec) < 0)
    {
        tu12 = (1000000 + time2.tv_usec) - time1->tv_usec;
        ts12 = (time2.tv_sec - time1->tv_sec) - 1;
    }
    else
    {
        tu12 = time2.tv_usec - time1->tv_usec;
        ts12 = time2.tv_sec - time1->tv_sec;
    }

    snprintf(buf, size_buf, "Time: %lu.%06lu sec", ts12, tu12);
}
//======================================================================
int child_proc(int numProc, const char *buf_req)
{
    struct timeval time1;
    int i;
    char s[256];

    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1)
    {
        printf("<%s:%d> Error getrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
    }
    else
    {
        if ((conf->num_connections + 5) > (long)lim.rlim_cur)
        {
            if ((conf->num_connections + 5) <= (long)lim.rlim_max)
            {
                lim.rlim_cur = conf->num_connections + 5;
                setrlimit(RLIMIT_NOFILE, &lim);
            }
            else
            {
                printf("<%s:%d> Error lim.rlim_max=%ld\n", __func__, __LINE__, (long)lim.rlim_max);
                exit(1);
            }
        }
    }

    time_t now;
    time(&now);
    printf("[%d] pid: %d,  %s", numProc, getpid(), ctime(&now));

    thread thr;
    try
    {
        thr = thread(thr_client, numProc);
    }
    catch (...)
    {
        fprintf(stderr, "[%d] <%s:%d> Error create thread(cgi_handler): errno=%d\n", numProc, __func__, __LINE__, errno);
        exit(errno);
    }

gettimeofday(&time1, NULL);
    i = 0;
    int all_conn = 0;
    while (i < conf->num_connections)
    {
        Connect *req = new(nothrow) Connect;
        if (!req)
        {
            fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
            exit(1);
        }

        req->servSocket = conf->create_sock(conf->ip, conf->port);
        if (req->servSocket < 0)
        {
            exit(1);
        }

        ++all_conn;

        req->num_conn = i;
        req->num_req = 0;
        req->req.p = buf_req;
        req->req.len = strlen(buf_req);
        req->req.i = 0;
        req->read_bytes = 0;
        
        push_to_wait_list(req);
        
        ++i;
    }

    thr.join();

get_time_connect(&time1, s, sizeof(s));

    printf("-[%d]  %s, all_conn=%d, good_conn=%d, good_req=%d\n"
           "       all read = %lld\n", numProc, s, all_conn, get_good_conn(), get_good_req(), get_all_read());

    exit(0);    
}
