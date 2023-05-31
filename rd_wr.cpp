#include "client.h"

using namespace std;

void get_time(char *s, int size);
/*====================================================================*/
int wait_read(int fd, int timeout)
{
    struct pollfd fdrd;
    int ret, tm, errno_ = 0;
    
    if (timeout == -1)
        tm = -1;
    else
        tm = timeout * 1000;

    fdrd.fd = fd;
    fdrd.events = POLLIN;
retry:
    ret = poll(&fdrd, 1, tm);
    if (ret == -1)
    {
        errno_ = errno;
        printf("<%s:%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
        if (errno == EINTR)
            goto retry;
        return -errno_;
    }
    else if (!ret)
        return -408;
    
    if (fdrd.revents & POLLIN)
        return 1;
    else if (fdrd.revents & POLLHUP)
        return 0;

    return -1;
}
/*====================================================================*/
static int wait_write(int fd, int timeout)
{
    int ret, errno_ = 0;
    struct pollfd fdwr;

    fdwr.fd = fd;
    fdwr.events = POLLOUT;
retry:
    ret = poll(&fdwr, 1, timeout * 1000);
    if (ret == -1)
    {
        errno_ = errno;
        printf("<%s():%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
        if (errno == EINTR)
              goto retry;
        return -errno_;
    }
    else if (!ret)
        return -408;

    if (fdwr.revents == POLLOUT)
        return 1;

    printf("<%s():%d> Error fdwr.revents = 0x%02x\n", __func__, __LINE__, fdwr.revents);
    return -1;
}
//======================================================================
int read_timeout(int fd, char *buf, int len, int timeout)
{
    int read_bytes = 0, ret, tm, errno_ = 0;
    struct pollfd fdrd;
    char *p;
    
    tm = (timeout == -1) ? -1 : (timeout * 1000);

    fdrd.fd = fd;
    fdrd.events = POLLIN;
    p = buf;
    while (len > 0)
    {
        ret = poll(&fdrd, 1, tm);
        if (ret == -1)
        {
            errno_ = errno;
            printf("<%s():%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            return -errno_;
        }
        else if (!ret)
            return -408;

        if (fdrd.revents & POLLERR)
        {
            printf("<%s:%d> POLLERR fdrd.revents = 0x%02x\n", __func__, __LINE__, fdrd.revents);
            return -__LINE__;
        }
        else if (fdrd.revents & POLLIN)
        {
            ret = read(fd, p, len);
            if (ret == -1)
            {
                errno_ = errno;
                printf("<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
                return -errno_;
            }
            else if (ret == 0)
                break;
            else
            {
                p += ret;
                len -= ret;
                read_bytes += ret;
            }
        }
        else if (fdrd.revents & POLLHUP)
            break;
    }

    return read_bytes;
}
/*====================================================================*/
int write_timeout(int fd, const char *buf, int len, int timeout)
{
    int write_bytes = 0, ret, errno_ = 0;

    while (len > 0)
    {
        ret = wait_write(fd, timeout);
        if (ret < 0)
            return ret;

        ret = write(fd, buf, len);
        if (ret == -1)
        {
            errno_ =errno;
            printf("<%s():%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            return -errno_;
        }

        write_bytes += ret;
        len -= ret;
        buf += ret;
    }

    return write_bytes;
}
/*====================================================================*/
int read_line_sock(int fd, char *buf, int size, int timeout)
{   
    int ret, n, read_bytes = 0;
    char *p = NULL;

    for ( ; size > 0; )
    {
        ret = wait_read(fd, timeout);
        if (ret < 0)
            return ret;

        ret = recv(fd, buf, size, MSG_PEEK);
        if (ret > 0)
        {
            if ((p = (char*)memchr(buf, '\n', ret)))
            {
                n = p - buf + 1;
                ret = recv(fd, buf, n, 0);
                if (ret <= 0)
                {
                    return -errno;
                }
                return read_bytes + ret;
            }
            n = recv(fd, buf, ret, 0);
            if (n != ret)
            {
                return -__LINE__;
            }
            buf += n;
            size -= n;
            read_bytes += n;
        }
        else // ret <= 0
        {
            return -errno;
        }
    }

    return -414;
}
//======================================================================
int read_headers_to_stdout(Connect *resp)
{
    int ret, read_bytes = 0, i = 0;

    for ( ; ; ++i)
    {
        ret = read_line_sock(resp->servSocket, 
                            resp->resp.buf, 
                            SIZE_BUF - 1,
                            conf->Timeout);
        if (ret <= 0)
            goto exit_;

        read_bytes += ret;
        *(resp->resp.buf + ret) = 0;
        printf("%s", resp->resp.buf);
        if (i == 0)
            sscanf(resp->resp.buf, "%*s %d %*s", &resp->respStatus);
        
        ret = strcspn(resp->resp.buf, "\r\n");
        if (ret == 0)
        {
            ret = read_bytes;
            goto exit_;
        }
    }
    printf("<%s():%d>  Error read_headers(): ?\n", __func__, __LINE__);

exit_:
    return ret;
}
//======================================================================
int read_line(FILE *f, char *s, int size)
{
    char *p = s;
    int ch, len = 0, wr = 1;

    while (((ch = getc(f)) != EOF) && (len < size))
    {
        if (ch == '\n')
        {
            *p = 0;
            if (wr == 0)
            {
                wr = 1;
                continue;
            }
            return len;
        }
        else if (wr == 0)
            continue;
        else if (ch == '#')
        {
            wr = 0;
        }
        else if (ch != '\r')
        {
            *(p++) = (char)ch;
            ++len;
        }
    }
    *p = 0;

    return len;
}
//======================================================================
int read_req_file_(FILE *f, char *req, int size)
{
    *req = 0;
    char *p = req;
    int len = 0, read_startline = 0;

    while (len < size)
    {
        int n = read_line(f, p, size - len);
        if (n > 0)
        {
            len += n;
            int m = strlen(end_line);
            if ((len + m) < size)
            {
                if (read_startline == 0)
                {
                    if (sscanf(p, "%15s %1023s %*s", Method, Uri) != 2)
                        return -4;
                    read_startline = 1;
                }
                else
                {
                    if (strstr_case(p, "Connection"))
                    {
                        if (strstr_case(p + 11, "close"))
                            connKeepAlive = 0;
                        else
                            connKeepAlive = 1;
                    }
                    else if (strstr_case(p, "Host:"))
                    {
                        if (sscanf(p, "Host: %127s", Host) != 1)
                        {
                            fprintf(stderr, "Error: [%s]\n", p);
                            return -1;
                        }
                    }
                }

                strcat(p, end_line);
                len += m;
                p += (n + m);
            }
            else
                return -1;
        }
        else if (n == 0)
        {
            if (feof(f))
                return len;

            if (read_startline == 0)
            {
                int m = strlen(end_line);
                if ((len + m) < size)
                {
                    memcpy(p, end_line, m + 1);
                    len += m;
                    p += m;
                }
                else
                    return -3;
            }
            else
            {
                int m = strlen(end_line);
                if ((len + m) < size)
                {
                    memcpy(p, end_line, m + 1);
                    len += m;
                    p += m;
                    if (strcmp(Method, "POST"))
                        return len;
                    else
                    {
                        int ret;
                        while (len < size)
                        {
                            if ((ret = fread(p, 1, size - len - 1, f)) <= 0)
                                return len;
                            len += ret;
                            p += ret;
                        }
                        *p = 0;
                        if (feof(f))
                            return len;
                        else
                            return -1;
                    }
                }
            }
        }
        else
            return n;
    }
    
    if (feof(f))
        return len;
    return -1;
}
//======================================================================
int read_req_file(const char *path, char *req, int size)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, " Error open request file(%s): %s\n", path, strerror(errno));
        return -1;
    }

    int n = read_req_file_(f, req, size);
    if (n <= 0)
    {
        fprintf(stderr, "<%s> Error read_req_file()=%d\n", __func__, n);
        fclose(f);
        return -1;
    }

    fclose(f);
    return n;
}
//======================================================================
int write_to_serv(Connect *req, const char *buf, int len)
{
    if (len == 0)
    {
        fprintf(stderr, "<%s:%d:%d:%d> Error len=0\n", __func__, __LINE__, req->num_conn, req->num_req);
        return -1;
    }
    int ret = send(req->servSocket, buf, len, 0);
    if (ret == -1)
    {
        fprintf(stderr, "<%s:%d:%d:%d> Error send(): %s\n", __func__, __LINE__, req->num_conn, req->num_req, strerror(errno));
        if (errno == EAGAIN)
            return ERR_TRY_AGAIN;
        else 
            return -1;
    }
    else
        return  ret;
}
//======================================================================
int read_from_server(Connect *req, char *buf, int len)
{
    if (len == 0)
    {
        fprintf(stderr, "<%s:%d:%d:%d> len=0\n", 
                    __func__, __LINE__, req->num_conn, req->num_req);
        return 0;
    }

    int ret = recv(req->servSocket, buf, len, 0);
    if (ret == -1)
    {
        if (errno == EAGAIN)
            return ERR_TRY_AGAIN;
        else
        {
            fprintf(stderr, "<%s:%d:%d:%d> Error recv(): %s\n", __func__, __LINE__, req->num_conn, req->num_req, strerror(errno));
            return -1;
        }
    }
    else
        return  ret;
}
//======================================================================
int find_empty_line(Connect *req)
{
    char *pCR, *pLF;
    while (req->resp.lenTail > 0)
    {
        int i = 0, len_line = 0;
        pCR = pLF = NULL;
        while (i < req->resp.lenTail)
        {
            char ch = *(req->resp.p_newline + i);
            if (ch == '\r')// found CR
            {
                if (i == (req->resp.lenTail - 1))
                    return 0;
                if (pCR)
                    return -1;
                pCR = req->resp.p_newline + i;
            }
            else if (ch == '\n')// found LF
            {
                pLF = req->resp.p_newline + i;
                if ((pCR) && ((pLF - pCR) != 1))
                    return -1;
                i++;
                break;
            }
            else
                len_line++;
            i++;
        }

        if (pLF) // found end of line '\n'
        {
            if (pCR == NULL)
                *pLF = 0;
            else
                *pCR = 0;

            if (len_line == 0)
            {
                req->resp.lenTail -= i;
                if (req->resp.lenTail > 0)
                    req->resp.ptr = pLF + 1;
                else
                    req->resp.ptr = NULL;
                return 1;
            }

//fprintf(stderr, "[>%s]\n", req->resp.p_newline);

            if (!strlcmp_case(req->resp.p_newline, "HTTP/", 5))
            {
                req->respStatus = atoi(req->resp.p_newline + 9);
            }
            else if (memchr(req->resp.p_newline, ':', len_line))
            {
                int n = parse_headers(req, req->resp.p_newline);
                if (n < 0)
                    return n;
            }
            else
            {
                return -1;
            }

            req->resp.lenTail -= i;
            req->resp.p_newline = pLF + 1;
        }
        else if (pCR && (!pLF))
            return -1;
        else
            break;
    }

    return 0;
}
//======================================================================
int read_http_headers(Connect *r)
{
    int len = SIZE_BUF - r->resp.len - 1;
    if (len <= 0)
    {
        fprintf(stderr, "<%s:%d:%d:%d> Error: empty line not found\n", __func__, __LINE__, r->num_conn, r->num_req);
        return -1;
    }

    int ret = read_from_server(r, r->resp.buf + r->resp.len, len);
    if (ret < 0)
    {
        if (ret == ERR_TRY_AGAIN)
            return ERR_TRY_AGAIN;
        return -1;
    }
    else if (ret == 0)
    {
        fprintf(stderr, "<%s:%d:%d:%d> Error server hung up\n", __func__, __LINE__, r->num_conn, r->num_req);
        return -1;
    }

    r->resp.lenTail += ret;
    r->resp.len += ret;
    r->resp.buf[r->resp.len] = 0;

    ret = find_empty_line(r);
    if (ret == 1) // empty line found
    {
        return 1;
    }
    else if (ret < 0) // error
    {
        fprintf(stderr, "<%s:%d> Error find_empty_line()=%d\n", __func__, __LINE__, ret);
        return -1;
    }

    return 0;
}
