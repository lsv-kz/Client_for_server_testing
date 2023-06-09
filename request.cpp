#include "client.h"

using namespace std;

static Connect *work_list_start = NULL;
static Connect *work_list_end = NULL;

static Connect *wait_list_start = NULL;
static Connect *wait_list_end = NULL;

static mutex mtx_;
static condition_variable cond_;

static struct pollfd *poll_fd;

static int good_conn = 0, good_req = 0, num_conn, all_conn;
static long long allRD = 0;
static int num_poll;

static void worker(Connect *r);
int get_size_chunk(Connect *r);
int chunk(Connect *r);
//======================================================================
int get_good_req(void)
{
    return good_req;
}
//======================================================================
int get_good_conn(void)
{
    return good_conn;
}
//======================================================================
long long get_all_read(void)
{
    return allRD;
}
//======================================================================
static void del_from_list(Connect *r)
{
    if (r->prev && r->next)
    {
        r->prev->next = r->next;
        r->next->prev = r->prev;
    }
    else if (r->prev && !r->next)
    {
        r->prev->next = r->next;
        work_list_end = r->prev;
    }
    else if (!r->prev && r->next)
    {
        r->next->prev = r->prev;
        work_list_start = r->next;
    }
    else if (!r->prev && !r->next)
        work_list_start = work_list_end = NULL;
}
//======================================================================
static void add_work_list()
{
mtx_.lock();
    if (wait_list_start)
    {
        if (work_list_end)
            work_list_end->next = wait_list_start;
        else
            work_list_start = wait_list_start;

        wait_list_start->prev = work_list_end;
        work_list_end = wait_list_end;
        wait_list_start = wait_list_end = NULL;
    }
mtx_.unlock();
}
//======================================================================
static void end_request(Connect *r)
{
    if ((r->err == 0) && (r->respStatus < 300))
    {
        ++good_req;
        r->num_req++;
        
        if ((r->num_req < conf->num_requests) && r->connKeepAlive)
        {
            r->operation = SEND_REQUEST;
            push_to_wait_list(r);
            return;
        }
    }
    else
    {
        fprintf(stderr, "[%d/%d/%d]<%s:%d> [%d] read_bytes=%lld\n", r->num_proc, r->num_conn, r->num_req,  
                __func__, __LINE__, r->respStatus, r->read_bytes);
    }

    shutdown(r->servSocket, SHUT_RDWR);
    close(r->servSocket);
    delete r;
mtx_.lock();
    ++num_conn;
mtx_.unlock();
}
//======================================================================
static int set_poll(int num_proc)
{
    num_poll = 0;
    time_t t = time(NULL);
    Connect *r = work_list_start, *next = NULL;
    for ( ; r; r = next)
    {
        next = r->next;

        if (r->sock_timer == 0)
            r->sock_timer = t;

        if ((t - r->sock_timer) >= conf->Timeout)
        {
            fprintf(stderr, "[%d]<%s:%d> Timeout=%ld, %s\n", num_proc, __func__, __LINE__, 
                            t - r->sock_timer, get_str_operation(r->operation));
            r->err = -1;
            del_from_list(r);
            end_request(r);
        }
        else
        {
            poll_fd[num_poll].fd = r->servSocket;
            poll_fd[num_poll].events = r->event;
            ++num_poll;
        }
    }

    return num_poll;
}
//======================================================================
static int poll_worker(int num_proc)
{
    if (num_poll <= 0)
        return 0;
    int ret = poll(poll_fd, num_poll, conf->TimeoutPoll);
    if (ret == -1)
    {
        fprintf(stderr, "[%d]<%s:%d> Error poll(): %s\n", num_proc, __func__, __LINE__, strerror(errno));
        return -1;
    }
    else if (ret == 0)
        return 0;

    Connect *r = work_list_start, *next = NULL;
    int i = 0;
    for ( ; (i < num_poll) && (ret > 0) && r; r = next, ++i)
    {
        next = r->next;

        if (poll_fd[i].revents == POLLOUT)
        {
            --ret;
            worker(r);
        }
        else if (poll_fd[i].revents & POLLIN)
        {
            --ret;
            worker(r);
        }
        else if (poll_fd[i].revents)
        {
            --ret;
            fprintf(stderr, "<%s:%d> Error: events=0x%x(0x%x)\n", __func__, __LINE__, 
                                                poll_fd[i].events, poll_fd[i].revents);
            r->err = -1;
            del_from_list(r);
            end_request(r);
        }
    }

    return 0;
}
//======================================================================
void thr_client(int num_proc)
{
    int Timeout = 5;
    num_conn = 0;
    all_conn = conf->num_connections;
    
    poll_fd = new(nothrow) struct pollfd [conf->num_connections];
    if (!poll_fd)
    {
        fprintf(stderr, "[%d]<%s:%d> Error malloc(): %s\n", num_proc, __func__, __LINE__, strerror(errno));
        exit(1);
    }
    
    while (1)
    {
        {
    unique_lock<mutex> lk(mtx_);
            while ((!work_list_start) && (!wait_list_start) && (num_conn < all_conn))
            {
                if (cond_.wait_for(lk, chrono::milliseconds(1000 * Timeout)) == cv_status::timeout)
                {
                    fprintf(stderr, "<%s:%d> Timeout wait_for(): %d s, num_conn=%d\n", 
                                    __func__, __LINE__, Timeout, num_conn);
                    num_conn = all_conn;
                }
            }

            if (num_conn >= all_conn)
                break;
        }

        add_work_list();
        set_poll(num_proc);
        if (poll_worker(num_proc) < 0)
            break;
    }

    delete [] poll_fd;
}
//======================================================================
void push_to_wait_list(Connect *r)
{
    r->err = 0;
    r->respStatus = 0;
    r->event = POLLOUT;
    r->sock_timer = 0;
    r->read_bytes = 0;
    r->req.i = 0;
    r->chunk.chunk = 0;
    r->chunk.end = 0;
    r->next = NULL;
mtx_.lock();
    r->prev = wait_list_end;
    if (wait_list_start)
    {
        wait_list_end->next = r;
        wait_list_end = r;
    }
    else
        wait_list_start = wait_list_end = r;
mtx_.unlock();
    cond_.notify_one();
}
//======================================================================
void set_all_conn(int n)
{
mtx_.lock();
    all_conn = n;
mtx_.unlock();
    cond_.notify_one();
}
//======================================================================
int send_headers(Connect *r)
{
    int wr = write_to_serv(r, r->req.ptr + r->req.i, r->req.len - r->req.i);
    if (wr < 0)
    {
        if (wr == ERR_TRY_AGAIN)
            return ERR_TRY_AGAIN;
        else
            return -1;
    }
    else if (wr > 0)
    {
        r->req.i += wr;
    }

    return wr;
}
//======================================================================
static void worker(Connect *r)
{
    if ((r->operation == SEND_REQUEST) || (r->operation == CONNECT))
    {
        if (r->operation == CONNECT)
        {
            r->operation = SEND_REQUEST;
            ++good_conn;
        }

        int wr = send_headers(r);
        if (wr > 0)
        {
            if ((r->req.len - r->req.i) == 0)
            {
                r->sock_timer = 0;
                r->operation = READ_RESP_HEADERS;
                r->event = POLLIN;
                r->resp.len = r->resp.lenTail = 0;
                r->resp.ptr = NULL;
                r->resp.p_newline = r->resp.buf;
                r->cont_len = 0;
            }
            else
                r->sock_timer = 0;
        }
        else if (wr < 0)
        {
            if (wr == ERR_TRY_AGAIN)
            {
                //fprintf(stderr, "[%d/%d/%d]<%s:%d> TRY_AGAIN\n", r->num_proc, r->num_conn, r->num_req,  
                //                        __func__, __LINE__);
            }
            else
            {
                r->err = -1;
                del_from_list(r);
                end_request(r);
            }
        }
    }
    else if (r->operation == READ_RESP_HEADERS)
    {
        int ret = read_http_headers(r);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                ;
            else
            {
                r->err = -1;
                del_from_list(r);
                end_request(r);
            }
        }
        else if (ret > 0)
        {
            allRD += r->resp.lenTail;
            r->read_bytes += r->resp.lenTail;
            r->operation = READ_ENTITY;
            if (!strcmp(Method, "HEAD"))
            {
                del_from_list(r);
                end_request(r);
                return;
            }

            r->sock_timer = 0;
            r->resp.len = r->resp.lenTail;
            r->resp.lenTail = 0;
            if (r->chunk.chunk)
            {
                int ret = chunk(r);
                if (ret < 0)
                {
                    r->err = -1;
                    del_from_list(r);
                    end_request(r);
                }
                else if (ret > 0)
                {
                    del_from_list(r);
                    end_request(r);
                }
            }
            else
            {
                r->cont_len -= r->resp.len;
                if (r->cont_len == 0)
                {
                    del_from_list(r);
                    end_request(r);
                }
            }
        }
        else // ret == 0
            r->sock_timer = 0;
    }
    else if (r->operation == READ_ENTITY)
    {
        if (r->chunk.chunk == 0)
        {
            char  buf[SIZE_BUF];
            int len = (r->cont_len > SIZE_BUF) ? SIZE_BUF : r->cont_len;
            if (len == 0)
            {
                del_from_list(r);
                end_request(r);
                return;
            }

            int ret = read_from_server(r, buf, len);
            //int ret = read_from_server(r, r->resp.buf, len);
            if (ret < 0)
            {
                if (ret != ERR_TRY_AGAIN)
                {
                    r->err = -1;
                    del_from_list(r);
                    end_request(r);
                }
            }
            else if (ret == 0)
            {
                fprintf(stderr, "<%s:%d:%d> read_from_server()=0\n", 
                            __func__, __LINE__, r->num_req);
                r->err = -1;
                del_from_list(r);
                end_request(r);
            }
            else
            {
                allRD += ret;
                r->read_bytes += ret;
                r->cont_len -= ret;
                if (r->cont_len == 0)
                {
                    del_from_list(r);
                    end_request(r);
                }
            }
        }
        else
        {
            if (r->chunk.size > 0)
            {
                r->resp.ptr = r->resp.buf;
                int len = (r->chunk.size > SIZE_BUF) ? SIZE_BUF : r->chunk.size;
                int ret = read_from_server(r, r->resp.buf, len);
                if (ret < 0)
                {
                    if (ret != ERR_TRY_AGAIN)
                    {
                        r->err = -1;
                        del_from_list(r);
                        end_request(r);
                    }
                }
                else if (ret == 0)
                {
                    fprintf(stderr, "<%s:%d:%d> read_from_server()=0\n", 
                                __func__, __LINE__, r->num_req);
                    r->err = -1;
                    del_from_list(r);
                    end_request(r);
                }
                else
                {
                    allRD += ret;
                    r->read_bytes += ret;
                    r->chunk.size -= ret;
                    if (r->chunk.size == 0)
                    {
                        r->chunk.size = -1;
                        if (r->chunk.end)
                        {
                            del_from_list(r);
                            end_request(r);
                            return;
                        }
                    }
                    r->sock_timer = 0;
                }
            }
            else
            {
                if (r->chunk.size == -1)
                {
                    int len = SIZE_BUF - r->resp.len - 1;
                    int ret = read_from_server(r, r->resp.buf + r->resp.len, len);
                    if (ret < 0)
                    {
                        if (ret != ERR_TRY_AGAIN)
                        {
                            r->err = -1;
                            del_from_list(r);
                            end_request(r);
                        }
                    }
                    else if (ret == 0)
                    {
                        fprintf(stderr, "<%s:%d:%d:%d> read_from_server()=0/%lld\n", 
                                        __func__, __LINE__, r->num_conn, r->num_req, r->read_bytes);
                        r->err = -1;
                        del_from_list(r);
                        end_request(r);
                    }
                    else
                    {
                        allRD += ret;
                        r->read_bytes += ret;
                        r->resp.len += ret;
                        ret = chunk(r);
                        if (ret < 0)
                        {
                            r->err = -1;
                            del_from_list(r);
                            end_request(r);
                        }
                        else if (ret > 0)
                        {
                            del_from_list(r);
                            end_request(r);
                        }
                        else
                        {
                            
                        }
                    }
                }
                else
                {
                    fprintf(stderr, "<%s:%d:%d:%d> r->chunk.size=%ld\n", __func__, __LINE__, r->num_conn, r->num_req, r->chunk.size);
                }
            }
        }
    }
}
//======================================================================
int chunk(Connect *r)
{
    if (!r->resp.ptr || !r->resp.len)
    {
        r->resp.len = 0;
        r->resp.ptr = r->resp.buf;
        r->chunk.size = -1;
        return 0;
    }

    while (1)
    {
        r->chunk.size = get_size_chunk(r);
        if (r->chunk.size > 0)
        {
            if (r->resp.len > (r->chunk.size + 2))
            {
                r->resp.ptr += (r->chunk.size + 2);
                r->resp.len -= (r->chunk.size + 2);
                continue;
            }
            else if (r->resp.len == (r->chunk.size + 2))
            {
                r->resp.len = 0;
                r->resp.ptr = r->resp.buf;
                r->chunk.size = -1;
                break;
            }
            else // r->resp.len < (r->chunk.size + 2)
            {
                r->chunk.size -= (r->resp.len - 2);
                r->resp.len = 0;
                break;
            }
        }
        else if (r->chunk.size == 0)
        {
            r->chunk.end = 1;
            if (r->resp.len == 2)
            {
                return 1;
            }
            else if (r->resp.len < 2)
            {
                r->chunk.size = 2 - r->resp.len;
                r->resp.len = 0;
                break;
            }
            else
            {
                fprintf(stderr, "<%s:%d:%d:%d> ??? r->resp.len=%ld\n", 
                        __func__, __LINE__, r->num_conn, r->num_req, r->resp.len);
                return -1;
            }
        }
        else
        {
            if (r->chunk.size == ERR_TRY_AGAIN)
            {
                memmove(r->resp.buf, r->resp.ptr, r->resp.len);
                r->resp.ptr = r->resp.buf;
                r->chunk.size = -1;
                break;
            }
            else
                return -1;
        }
    }
    return 0;
}
