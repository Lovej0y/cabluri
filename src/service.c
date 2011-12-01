/*
  Returned status: OK, BADREQ, BADFMT, <VERSION>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>


#define VERSION             "LIBERTE CABLE 2.0"
#define REQVAR              "PATH_INFO"
#define MAX_REQ_LENGTH      512

/* caller shouldn't be able to differentiate OK/ERROR */
#define OK                  "OK"
#define BADREQ              "BADREQ"
#define BADFMT              "BADFMT"
#define BADCFG              "BADCFG"
#ifndef ERROR
#define ERROR               OK
#endif

#define MSGID_LENGTH         40
#define TOR_HOSTNAME_LENGTH  16
#define I2P_HOSTNAME_LENGTH  52
#define USERNAME_LENGTH      32
#define MAC_LENGTH          128

/* maximum filename length in (r)q_pfx/<msgid>/: 32 characters */
#define MAX_PATH_PREFIX     127
#define MAX_PATH_LENGTH     (MAX_PATH_PREFIX + sizeof(RQUEUE_SUFFIX)-1 + MSGID_LENGTH+1 + 32)

#define CABLE_QUEUES        "CABLE_QUEUES"
#define QUEUE_SUFFIX        "/queue/"
#define RQUEUE_SUFFIX       "/rqueue/"


static void retstatus(const char *status) {
    puts(status);
    exit(0);
}


/* lowercase hexadecimal */
static int vfyhex(int sz, const char *s) {
    if (strlen(s) != sz)
        return 0;

    for (; *s; ++s)
        if (!(isxdigit(*s) && !isupper(*s)))
            return 0;

    return 1;
}


/* lowercase Base-32 encoding (a-z, 2-7) */
static int vfybase32(int sz, const char *s) {
    if (strlen(s) != sz)
        return 0;

    for (; *s; ++s)
        if (!(islower(*s) || (*s >= '2' && *s <= '7')))
            return 0;

    return 1;
}


/* lowercase hostnames: recognizes .onion and .b32.i2p addresses */
static int vfyhost(char *s) {
    int  result = 0;
    char *dot   = strchr(s, '.');

    if (dot) {
        *dot = '\0';

        /* Tor .onion hostnames */
        if (!strcmp("onion", dot+1))
            result = vfybase32(TOR_HOSTNAME_LENGTH, s);

        /* I2P .b32.i2p hostnames */
        else if (!strcmp("b32.i2p", dot+1))
            result = vfybase32(I2P_HOSTNAME_LENGTH, s);

        *dot = '.';
    }

    return result;
}


static void write_line(const char *path, const char *s) {
    FILE *file;

    if (!(file = fopen(path, "w")))
        retstatus(ERROR);

    if (s) {
        if(fputs(s, file) < 0 || fputc('\n', file) != '\n') {
            fclose(file);
            retstatus(ERROR);
        }
    }

    if (fclose(file))
        retstatus(ERROR);
}


static void create_file(const char *path) {
    if (access(path, F_OK))
        write_line(path, NULL);
}


static void read_line(const char *path, char *s, int sz) {
    FILE *file;

    if (!(file = fopen(path, "r")))
        retstatus(ERROR);

    if (s) {
        if(!fgets(s, sz, file) || fgetc(file) != EOF) {
            fclose(file);
            retstatus(ERROR);
        }

        sz = strlen(s);
        if (s[sz-1] == '\n')
            s[sz-1] = '\0';
    }

    if (fclose(file))
        retstatus(ERROR);
}


static void check_file(const char *path) {
    if(access(path, F_OK))
        retstatus(ERROR);
}


static void handle_msg(const char *msgid, const char *mac, const char *hostname,
                       const char *username, const char *cqueues) {
    char path[MAX_PATH_LENGTH+1], npath[MAX_PATH_LENGTH+4+1];
    int  baselen;

    /* base: .../cables/rqueue/<msgid> */
    strcpy(path, cqueues);
    strcat(path, RQUEUE_SUFFIX);
    strcat(path, msgid);

    /* checkno /cables/rqueue/<msgid> */
    if (!access(path, F_OK))
        retstatus(ERROR);

    /* temp base: .../cables/rqueue/<msgid>.new */
    strcpy(npath, path);
    strcat(npath, ".new");
    baselen = strlen(npath);

    /* create directory (ok if exists) */
    if (mkdir(npath, 0700) && errno != EEXIST)
        retstatus(ERROR);

    /* write hostname */
    strcpy(npath + baselen, "/hostname");
    write_line(npath, hostname);

    /* write username */
    strcpy(npath + baselen, "/username");
    write_line(npath, username);

    /* write send.mac */
    strcpy(npath + baselen, "/send.mac");
    write_line(npath, mac);

    /* create recv.req */
    strcpy(npath + baselen, "/recv.req");
    create_file(npath);

    /* rename .../cables/rqueue/<msgid>.new -> <msgid> */
    npath[baselen] = '\0';
    if (rename(npath, path))
        retstatus(ERROR);
}


static void handle_rcp(const char *msgid, const char *mac, const char *cqueues) {
    char path[MAX_PATH_LENGTH+1], npath[MAX_PATH_LENGTH+1];
    char exmac[MAC_LENGTH+2];
    int  baselen;

    /* base: .../cables/queue/<msgid> */
    strcpy(path, cqueues);
    strcat(path, QUEUE_SUFFIX);
    strcat(path, msgid);
    baselen = strlen(path);

    /* check send.ok */
    strcpy(path + baselen, "/send.ok");
    check_file(path);

    /* read recv.mac */
    strcpy(path + baselen, "/recv.mac");
    read_line(path, exmac, sizeof(exmac));

    /* compare <recvmac> <-> recv.mac */
    if (strcmp(mac, exmac))
        retstatus(ERROR);

    /* create ack.req (atomic) */
    strcpy(npath, path);
    strcpy(path  + baselen, "/send.ok");
    strcpy(npath + baselen, "/ack.req");
    if (! link(path, npath)) {
        /* touch /cables/queue/<msgid>/ (if ack.req didn't exist) */
        path[baselen] = '\0';

        if (utime(path, NULL))
            retstatus(ERROR);
    }
    else if (errno != EEXIST)
        retstatus(ERROR);
}


static void handle_ack(const char *msgid, const char *mac, const char *cqueues) {
    char path[MAX_PATH_LENGTH+1], trpath[MAX_PATH_LENGTH+4+1];
    char exmac[MAC_LENGTH+2];
    int  baselen;

    /* base: .../cables/rqueue/<msgid> */
    strcpy(path, cqueues);
    strcat(path, RQUEUE_SUFFIX);
    strcat(path, msgid);
    baselen = strlen(path);

    /* check recv.ok */
    strcpy(path + baselen, "/recv.ok");
    check_file(path);

    /* read ack.mac */
    strcpy(path + baselen, "/ack.mac");
    read_line(path, exmac, sizeof(exmac));

    /* compare <ackmac> <-> ack.mac */
    if (strcmp(mac, exmac))
        retstatus(ERROR);

    /* rename .../cables/rqueue/<msgid> -> <msgid>.del */
    path[baselen] = '\0';
    strcpy(trpath, path);
    strcat(trpath, ".del");

    if (rename(path, trpath))
        retstatus(ERROR);
}


int main() {
    char       buf[MAX_REQ_LENGTH+1], cqueues[MAX_PATH_PREFIX+1];
    const char *pathinfo, *delim = "/", *cqenv;
    char       *cmd, *msgid, *mac, *arg1, *arg2;

    umask(0077);
    setlocale(LC_ALL, "C");

    
    /* HTTP headers */
    printf("Content-Type: text/plain\n"
           "Cache-Control: no-cache\n\n");


    /* Check request availability and length */
    pathinfo = getenv(REQVAR);
    if (!pathinfo || strlen(pathinfo) >= sizeof(buf))
        retstatus(BADREQ);


    /* Copy request to writeable buffer */
    strncpy(buf, pathinfo, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';


    /* Tokenize the request */
    cmd   = strtok(buf,  delim);
    msgid = strtok(NULL, delim);
    mac   = strtok(NULL, delim);
    arg1  = strtok(NULL, delim);
    arg2  = strtok(NULL, delim);

    if (strtok(NULL, delim) || !cmd)
        retstatus(BADFMT);


    /* Get queues prefix from environment */
    cqenv = getenv(CABLE_QUEUES);
    if (!cqenv || strlen(cqenv) >= sizeof(cqueues))
        retstatus(BADCFG);

    strncpy(cqueues, cqenv, sizeof(cqueues)-1);
    cqueues[sizeof(cqueues)-1] = '\0';


    /* Handle commands

       ver
       msg/<msgid>/<mac>/<hostname>/<username>
       rcp/<msgid>/<mac>
       ack/<msgid>/<mac>

       msgid:    MSGID_LENGTH        lowercase xdigits
       mac:      MAC_LENGTH          lowercase xdigits
       hostname: TOR_HOSTNAME_LENGTH lowercase base-32 chars + ".onion"
                 I2P_HOSTNAME_LENGTH lowercase base-32 chars + ".b32.i2p"
       username: USERNAME_LENGTH     lowercase base-32 chars
    */
    if (!strcmp("ver", cmd)) {
        if (msgid)
            retstatus(BADFMT);

        retstatus(VERSION);
    }
    else if (!strcmp("msg", cmd)) {
        if (!arg2)
            retstatus(BADFMT);

        if (   !vfyhex(MSGID_LENGTH, msgid)
            || !vfyhex(MAC_LENGTH, mac)
            || !vfyhost(arg1)
            || !vfybase32(USERNAME_LENGTH, arg2))
            retstatus(BADFMT);

        handle_msg(msgid, mac, arg1, arg2, cqueues);
    }
    else if (!strcmp("rcp", cmd)) {
        if (!mac || arg1)
            retstatus(BADFMT);

        if (   !vfyhex(MSGID_LENGTH, msgid)
            || !vfyhex(MAC_LENGTH, mac))
            retstatus(BADFMT);

        handle_rcp(msgid, mac, cqueues);
    }
    else if (!strcmp("ack", cmd)) {
        if (!mac || arg1)
            retstatus(BADFMT);

        if (   !vfyhex(MSGID_LENGTH, msgid)
            || !vfyhex(MAC_LENGTH, mac))
            retstatus(BADFMT);

        handle_ack(msgid, mac, cqueues);
    }
    else
        retstatus(BADFMT);


    retstatus(OK);
    return 0;
}
