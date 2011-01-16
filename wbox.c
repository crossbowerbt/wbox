/* Wbox - HTTP fun tool, wirtten by Salvatore 'antirez' Sanfilippo
 *                       antirez at gmail dot org
 *
 * This software is released under the GPL license version 2
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>

#include "wbsignal.h"
#include "anet.h"
#include "sds.h"

/* Flags */
#define WBOX_NONE 0
#define WBOX_ACCEPT_COMPR 1
#define WBOX_USE_HEAD 2
#define WBOX_USE_HTTP10 4

/* Exit codes */
#define WBOX_EXIT_SUCCESS 0
#define WBOX_EXIT_BADARGS 1
#define WBOX_EXIT_RESOLV 2
#define WBOX_EXIT_CONN 3
#define WBOX_EXIT_IO 4
#define WBOX_EXIT_EOF 5

/* Hardcoded stuff */
#define WBOX_VERSION 1
#define WBOX_RECV_BUF (1024*4)
#define WBOX_TIMESPLIT_SAMPLES 40
/* the ANSI sequence to clear the current line
 * and move the curosr on the left */
#define WBOX_ANSI_CLEARLINE "\033[1K\033[G"

/* Useful defines */
#define WBOX_NOTUSED(V) ((void) V)

/* Data structures */

typedef struct wconfig {
    char *url;
    char *host;
    int dump;
    int compr;
    int head;
    int showhdr;
    int wait;
    int clients;
    int silent;
    int timesplit;
    int maxreq;
    int http10;
} wconfig;

typedef struct timesplit {
    int time;
    int firstbyte;
    int lastbyte;
} timesplit;

/* Reply info describes the HTTP reply we get from server */
typedef struct replyinfo {
    int code;
    char *reason;
    int replylen;
    int time;
    int compr;
    int tsamples; /* timesplit samples used */
    timesplit tsample[WBOX_TIMESPLIT_SAMPLES];
} replyinfo;

/* Url info describes an URL */
typedef struct urlinfo {
    char *proto;
    char *domain;
    int port;
    char *req;
} urlinfo;

long long milliseconds(void)
{
    struct timeval tmptv;

    gettimeofday(&tmptv, NULL);
    return ((long long)tmptv.tv_sec*1000)+(tmptv.tv_usec/1000);
}

int strisnumber(char *s) {
    while(*s == ' ' || (*s >= '0' && *s <= '9')) s++;
    return *s == '\0';
}

void parseUrl(char *url, urlinfo *ui)
{
    char *copy = sdsnew(url), *p, *d;

    /* protocol */
    p = strstr(copy,"://");
    if (!p) {
        ui->proto = sdsnew("http");
        p = copy;
    } else {
        *p='\0';
        ui->proto = sdsnew(copy);
        *p=':';
        p += 3; /* skip "://" */
    }
    /* domain */
    d = strchr(p,'/');
    if (!d) d = strchr(p,'?');
    if (!d) {
        ui->domain = sdsnew(p);
        d = "\0";
    } else {
        char saved = *d;
        *d = '\0';
        ui->domain = sdsnew(p);
        *d = saved;
        if (*d == '/') d++; /* skip "/" */
    }
    /* port */
    p = strchr(ui->domain,':');
    if (!p) {
        ui->port = 80;
    } else {
        *p = '\0';
        ui->port = atoi(p+1);
        if (ui->port == 0) ui->port = 80;
    }
    /* request */
    ui->req = sdsnew("/");
    ui->req = sdscat(ui->req,d);
    sdsfree(copy);
}

void freeUrl(urlinfo *ui) {
    if (ui->proto) sdsfree(ui->proto);
    if (ui->domain) sdsfree(ui->domain);
    if (ui->req) sdsfree(ui->req);
}

static char *createHttpReq(urlinfo *ui, int flags)
{
    char *r = sdsnew((flags&WBOX_USE_HEAD) ? "HEAD ":"GET ");
    r = sdscat(r, ui->req);
    r = sdscat(r, " HTTP/1.");
    r = sdscat(r, (flags & WBOX_USE_HTTP10) ? "0" : "1");
    r = sdscat(r, "\r\nHost: ");
    r = sdscat(r, ui->domain);
    if (ui->port != 80) {
        r = sdscatprintf(r,":%d",ui->port);
    }
    r = sdscat(r,"\r\n"
"User-Agent: Mozilla/5.0 Windows; U; Windows NT 5.1; en-US; rv:1.8.1.4) Gecko/20070515 Firefox/2.0.0.4\r\n"
"Accept: text/xml,application/xml,application/xhtml+xml,text/html;q=0.9,text/plain;q=0.8,image/png,*/*;q=0.5\r\n"
"Accept-Language: en-us,en;q=0.5\r\n");
    if (flags & WBOX_ACCEPT_COMPR)
        r = sdscat(r,"Accept-Encoding: gzip,deflate\r\n");
    r = sdscat(r,
"Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"
"Connection: close\r\n\r\n");
    return r;
}

static int extractReplyInfo(replyinfo *ri, char *buf, int buflen, wconfig *wc)
{
    char *hdr = sdsnewlen(buf,buflen); /* make a copy to play with it */
    char *code,*reason,*p;

    /* Make sure we don't go over the header */
    p = strstr(hdr,"\r\n\r\n");
    if (p) *p = '\0';
    if (wc->showhdr) {
        if (!wc->silent) printf("\n");
        printf("%s\n",hdr);
        if (!wc->silent) printf("\n");
    }
    ri->compr = strstr(hdr,"Content-Encoding: gzip") != NULL;

    /* Some ugly parsing required... */
    code = strchr(hdr,' ');
    if (!code) goto fmterr;
    code++;
    reason = strchr(code,' ');
    if (!reason) goto fmterr;
    *reason = '\0';
    reason++;
    p = strchr(reason,'\n');
    if (!p) goto fmterr;
    *p = '\0';
    p = strchr(reason,'\r');
    if (p) *p = '\0';
    
    /* We can now fill the struct */
    ri->code = atoi(code);
    ri->reason = sdsnew(reason);
    sdsfree(hdr);
    return 0;
fmterr:
    sdsfree(hdr);
    return 1;
}

void initReplyInfo(replyinfo *ri)
{
    ri->code = 0;
    ri->replylen = 0;
    ri->time = 0;
    ri->compr = 0;
    ri->tsamples = 0;
    ri->reason = NULL;
}

void freeReplyInfo(replyinfo *ri)
{
    if (ri->reason) sdsfree(ri->reason);
}

void copyReplyInfo(replyinfo *d, replyinfo *s)
{
    *d = *s;
    if (d->reason) d->reason = sdsdup(s->reason);
}

static int httpRequest(replyinfo *ri, char *ip, urlinfo *ui, wconfig *conf) {
    char err[ANET_ERR_LEN];
    char *req;
    int s, nwritten, totlen, reqflags = WBOX_NONE;
    long long stime = milliseconds();
    long long tsample_stime = milliseconds();
    long long stime_bps = 0; /* used for accurate bandwidth measuring */

    initReplyInfo(ri);
    /* Connect */
    s = anetTcpConnect(err, ip, ui->port);
    if (s == ANET_ERR) {
        fprintf(stderr, "Opening the connection: %s\n", err);
        exit(WBOX_EXIT_CONN);
    }
    /* Write the HTTP request */
    if (conf->compr) reqflags |= WBOX_ACCEPT_COMPR;
    if (conf->head) reqflags |= WBOX_USE_HEAD;
    if (conf->http10) reqflags |= WBOX_USE_HTTP10;
    req = createHttpReq(ui,reqflags);
    nwritten = write(s, req, sdslen(req));
    if (nwritten == -1) {
        perror("Sending the HTTP request");
        sdsfree(req);
        exit(WBOX_EXIT_IO);
    }
    /* Read the request */
    totlen = 0;
    while(1) {
        char buf[WBOX_RECV_BUF];
        int nread;

        nread = anetRead(s, buf, WBOX_RECV_BUF);
        if (nread == 0) break;
        if (nread == -1) {
            perror("Reading from socket");
            sdsfree(req);
            exit(WBOX_EXIT_IO);
        }
        /* Populare tsamples */
        if (conf->timesplit) {
            int lastsample = ri->tsamples == WBOX_TIMESPLIT_SAMPLES;
            timesplit *ts;
            if (lastsample)
                ts = &ri->tsample[WBOX_TIMESPLIT_SAMPLES-1];
            else
                ts = &ri->tsample[ri->tsamples];
            ts->time = (lastsample ? ts->time : 0) +
                        (int) (milliseconds()-tsample_stime);
            if (!lastsample) ts->firstbyte = totlen;
            ts->lastbyte = totlen+nread-1;
            if (!lastsample) ri->tsamples++;
            tsample_stime = milliseconds();
        }
        /* Get HTTP reply header information from the first chunk of data */
        if (totlen == 0) extractReplyInfo(ri,buf,nread,conf);

        if (conf->dump) {
            int lastsample = ri->tsamples == WBOX_TIMESPLIT_SAMPLES;
            fwrite(buf,nread,1,stdout);
            if (!lastsample && conf->timesplit) {
                int idx = ri->tsamples-1;
                printf("\n\n-----------------------------------------------\n");
                printf("CHUNK TIME INFORMATION: %d-%d -> %d ms\n",
                    ri->tsample[idx].firstbyte,
                    ri->tsample[idx].lastbyte,
                    ri->tsample[idx].time);
                printf("-----------------------------------------------\n\n");
            }
            fflush(stdout);
        }
        totlen += nread;
        if (!conf->dump && !conf->silent) {
            printf(WBOX_ANSI_CLEARLINE);
            printf("%d bytes readed",totlen);
            if (totlen == WBOX_RECV_BUF*4)
                stime_bps = milliseconds();
            if (totlen > WBOX_RECV_BUF*4) {
                int recvbytes = totlen-WBOX_RECV_BUF*4;
                int elapsed = (int) (milliseconds()-stime_bps);
                float kbs = ((float)recvbytes*1000/elapsed)/1024;
                printf(" (%.2f kbytes/s)",kbs);
            }
            fflush(stdout);
        }
    }
    /* Done, close the socket and calculate timings */
    close(s);
    sdsfree(req);
    ri->time = (int) (milliseconds()-stime);
    ri->replylen = totlen;
    return 0;
}

static void wboxHelp(void) {
    printf(
"Usage: wbox <url> [options ...]\n\n"
"options list:\n\n"
"<number>         - stop after <number> requests\n"
"compr            - send Accept-Encoding: gzip,deflate in request\n"
"showhdr          - show the HTTP reply header\n"
"dump             - show the HTTP reply header + body\n"
"silent           - don't show status lines\n"
"head             - use the HEAD method instead of GET\n"
"http10           - use HTTP/1.0 instead of HTTP/1.1\n"
"host <hostname>  - use <hostname> as Host: field in HTTP request\n"
"timesplit        - show transfer times for different data chunks\n"
"wait <number>    - wait <number> seconds between requests. Default 1.\n"
"clients <number> - spawn <number> concurrent clients (via fork()).\n"
"\n"
"-h or --help     - show this page, follow the standard to help new users.\n"
"-v               - show version.\n"
"\nExamples\n\n"
"wbox wikipedia.org                  (simplest, basic usage)\n"
"wbox wikipedia.org 3 compr wait 0   (three requests, compression, no delay)\n"
"wbox wikipedia.org 1 showhdr silent (just show the HTTP reply header)\n"
"wbox wikipedia.org timesplit        (show splitted time information)\n"
"wbox 1.2.3.4 host example.domain    (test a virtual domain at 1.2.3.4)\n"
"\n"
"More docs? there is a tutorial at http://hping.org/wbox\n"
    );
}

static void sigtermHandler(int signum)
{
    WBOX_NOTUSED(signum);
    fprintf(stderr, "user terminated\n");
    exit(WBOX_EXIT_SUCCESS);
}

/* return 0 for the parent, 1 for the child */
static int spawnChilds(int count)
{
    int j;
    for(j = 0; j < count; j++) {
        pid_t p = fork();
        if (p == -1) {
            perror("fork");
            return 0;
        }
        if (p == 0) return 1;
    }
    return 0;
}

static void renderTimesplit(replyinfo *ri) {
    int j;

    for (j = 0; j < ri->tsamples; j++) {
        printf("       [%d] %d-%d -> %d ms\n", j,
            ri->tsample[j].firstbyte,
            ri->tsample[j].lastbyte,
            ri->tsample[j].time);
    }
}

static void parseArgs(char **argv, int argc, wconfig *conf) {
    int j;

    memset(conf,0,sizeof(*conf));
    conf->wait = 1;
    conf->maxreq = -1;

    if (argc < 2) {
        wboxHelp();
        exit(WBOX_EXIT_BADARGS);
    }
    if (!strcmp(argv[1],"-h") || !strcmp(argv[1],"--help")) {
        wboxHelp();
        exit(WBOX_EXIT_SUCCESS);
    }
    if (!strcmp(argv[1],"-v")) {
        printf("WBox version %d\n", WBOX_VERSION);
        exit(WBOX_EXIT_SUCCESS);
    }

    /* Make sure it's possible to call wbox with every kind
     * of argument as url including "-h", using:
     * 
     *   wbox -- -h
     *
     * Hardly useful but there must be a way to do this for
     * the user.
    */
    conf->url = argv[1];
    if (argc > 2 && !strcmp(argv[1],"--")) {
        argv++;
        argc--;
        conf->url = argv[1];
    }

    /* Option parsing, in a more discorsive way compared to
     * the usual unix switches */
    for(j = 2; j < argc; j++) {
        int next = (j+1 != argc);
        if (!strcmp(argv[j],"dump")) {
            conf->dump=1;
        } else if (!strcmp(argv[j],"compr")) {
            conf->compr=1;
        } else if (!strcmp(argv[j],"head")) {
            conf->head=1;
        } else if (!strcmp(argv[j],"timesplit")) {
            conf->timesplit=1;
        } else if (!strcmp(argv[j],"showhdr")) {
            conf->showhdr=1;
        } else if (!strcmp(argv[j],"silent")) {
            conf->silent=1;
        } else if (!strcmp(argv[j],"http10")) {
            conf->http10=1;
        } else if (next && !strcmp(argv[j],"host")) {
            j++;
            conf->host = argv[j];
        } else if (next && !strcmp(argv[j],"wait")) {
            j++;
            conf->wait = atoi(argv[j]);
        } else if (next && !strcmp(argv[j],"clients")) {
            j++;
            conf->clients = atoi(argv[j]);
        } else if (!strcmp(argv[j],"-h") ||
                   !strcmp(argv[j],"--help")) {
            wboxHelp();
            exit(WBOX_EXIT_SUCCESS);
        } else if (strisnumber(argv[j]) && atoi(argv[j]) > 0) {
            conf->maxreq = atoi(argv[j]);
        } else {
            fprintf(stderr, "\n * Wrong option or params: %s\n\n", argv[j]);
            wboxHelp();
            exit(WBOX_EXIT_BADARGS);
        }
    }
}

int main(int argc, char **argv)
{
    char err[ANET_ERR_LEN];
    char ip[32];
    int reqid = 0;
    replyinfo ri, oldri;
    urlinfo ui;
    wconfig conf;

    parseArgs(argv,argc,&conf);
    parseUrl(conf.url,&ui);
    if (anetResolve(err,ui.domain,ip) != ANET_OK) {
        fprintf(stderr,"%s\n",err);
        exit(WBOX_EXIT_RESOLV);
    }

    if (conf.host != NULL) {
        sdsfree(ui.domain);
        ui.domain=sdsnew(conf.host);
    }

    if (!conf.silent) {
        printf("WBOX %s (%s) port %d",ui.domain,ip,ui.port);
        if (conf.compr) printf(" [compr]");
        if (conf.head) printf(" [head]");
        if (conf.wait != 1) printf(" [wait %d]",conf.wait);
        printf("\n");
    }

    if (conf.clients > 1) {
        if (spawnChilds(conf.clients-1)) {
            /* Childs specific code */
        } else {
            /* Parent specific code */
            Signal(SIGINT,sigtermHandler);
        }
    } else {
        Signal(SIGINT,sigtermHandler);
    }

    initReplyInfo(&oldri);
    while(1) {
        int flags = WBOX_NONE;
        if (conf.compr) flags |= WBOX_ACCEPT_COMPR;
        if (conf.head) flags |= WBOX_USE_HEAD;

        /* Request */
        httpRequest(&ri,ip,&ui,&conf);
        if (!conf.silent) {
            /* Print status line */
            printf(WBOX_ANSI_CLEARLINE);
            /* hostname id */
            printf("%d. %d %s",reqid,ri.code,ri.reason ? ri.reason : "()");
            /* reply length */
            if (reqid != 0 && oldri.replylen != ri.replylen)
                printf("    (%d)",ri.replylen);
            else
                printf("    %d",ri.replylen);
            printf(" bytes");
            /* request time */
            printf("    %d ms",ri.time);
            if (ri.compr) printf("    compr");
            printf("\n");
            if (conf.timesplit) renderTimesplit(&ri);
        }
        reqid++;
        if (reqid == conf.maxreq) break;
        copyReplyInfo(&oldri,&ri);
        freeReplyInfo(&ri);
        sleep(conf.wait);
    }
    freeUrl(&ui);
    freeReplyInfo(&oldri);
    return 0;
}
