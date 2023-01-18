/*
 * Copyright (C) 2022 Jakub Kruszona-Zawadzki, Tappest sp. z o.o.
 *
 * This file is part of MooseFS.
 *
 * MooseFS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 (only).
 *
 * MooseFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MooseFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02111-1301, USA
 * or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

#if defined(_THREAD_SAFE) || defined(_REENTRANT) || defined(_USE_PTHREADS)
#define USE_PTHREADS 1
#endif

#ifndef MFSMAXFILES
#define MFSMAXFILES 4096
#endif

#if defined(HAVE_MLOCKALL)
#if defined(HAVE_SYS_MMAN_H)
#include <sys/mman.h>
#endif
#if defined(HAVE_SYS_RESOURCE_H)
#include <sys/resource.h>
#endif
#if defined(RLIMIT_MEMLOCK) && defined(MCL_CURRENT) && defined(MCL_FUTURE)
#define MFS_USE_MEMLOCK 1
#endif
#endif

#if defined(HAVE_MALLOC_H)
#include <malloc.h>
#endif

#if defined(HAVE_LINUX_OOM_H)
#include <linux/oom.h>
#if defined(OOM_DISABLE) || defined(OOM_SCORE_ADJ_MIN)
#define OOM_ADJUSTABLE 1
#endif
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#ifdef USE_PTHREADS
#include <pthread.h>
#endif

#define STR_AUX(x) #x
#define STR(x) STR_AUX(x)

#include "mfsnode/init.h"
#include "mfsnode/node.h"

#include "clocks.h"
#include "crc.h"
#include "defaults.h"
#include "massert.h"
#include "portable.h"
#include "slogger.h"
#include "sockets.h"

// included for threadfn definitions
#include "bgjobs.h"
#include "csserv.h"
#include "hddspacemgr.h"
#include "mainserv.h"
#include "masterconn.h"
#include "random.h"

#define RM_RESTART 0
#define RM_START 1
#define RM_STOP 2
#define RM_RELOAD 3
#define RM_INFO 4
#define RM_TEST 5
#define RM_KILL 6
#define RM_TRY_RESTART 7
#define RM_RESTORE 8

typedef struct deentry {
    void (*fun)(void);
    char* fname;
    struct deentry* next;
} deentry;

static deentry* dehead = NULL;

typedef struct weentry {
    void (*fun)(void);
    char* fname;
    struct weentry* next;
} weentry;

static weentry* wehead = NULL;

typedef struct ceentry {
    int (*fun)(void);
    char* fname;
    struct ceentry* next;
} ceentry;

static ceentry* cehead = NULL;

typedef struct rlentry {
    void (*fun)(void);
    char* fname;
    struct rlentry* next;
} rlentry;

static rlentry* rlhead = NULL;

typedef struct inentry {
    void (*fun)(void);
    char* fname;
    struct inentry* next;
} inentry;

static inentry* inhead = NULL;

typedef struct kaentry {
    void (*fun)(void);
    char* fname;
    struct kaentry* next;
} kaentry;

static kaentry* kahead = NULL;

typedef struct pollentry {
    void (*desc)(struct pollfd*, uint32_t*);
    void (*serve)(struct pollfd*);
    char* dname;
    char* sname;
    struct pollentry* next;
} pollentry;

static pollentry* pollhead = NULL;

typedef struct eloopentry {
    void (*fun)(void);
    char* fname;
    struct eloopentry* next;
} eloopentry;

static eloopentry* eloophead = NULL;

typedef struct chldentry {
    pid_t pid;
    void (*fun)(int);
    char* fname;
    struct chldentry* next;
} chldentry;

static chldentry* chldhead = NULL;

typedef struct timeentry {
    uint64_t nextevent;
    uint64_t useconds;
    uint64_t usecoffset;
    void (*fun)(void);
    char* fname;
    struct timeentry* next;
} timeentry;

static timeentry* timehead = NULL;

#ifdef USE_PTHREADS
static pthread_mutex_t nowlock = PTHREAD_MUTEX_INITIALIZER;
#endif
static uint32_t now;
static uint64_t usecnow;
// static int alcnt=0;

static int signalpipe[2];

/* interface */

void main_destruct_register_fname(void (*fun)(void), const char* fname)
{
    deentry* aux = (deentry*)malloc(sizeof(deentry));
    passert(aux);
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = dehead;
    dehead = aux;
}

void main_canexit_register_fname(int (*fun)(void), const char* fname)
{
    ceentry* aux = (ceentry*)malloc(sizeof(ceentry));
    passert(aux);
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = cehead;
    cehead = aux;
}

void main_wantexit_register_fname(void (*fun)(void), const char* fname)
{
    weentry* aux = (weentry*)malloc(sizeof(weentry));
    passert(aux);
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = wehead;
    wehead = aux;
}

void main_reload_register_fname(void (*fun)(void), const char* fname)
{
    rlentry* aux = (rlentry*)malloc(sizeof(rlentry));
    passert(aux);
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = rlhead;
    rlhead = aux;
}

void main_info_register_fname(void (*fun)(void), const char* fname)
{
    inentry* aux = (inentry*)malloc(sizeof(inentry));
    passert(aux);
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = inhead;
    inhead = aux;
}

void main_keepalive_register_fname(void (*fun)(void), const char* fname)
{
    kaentry* aux = (kaentry*)malloc(sizeof(kaentry));
    passert(aux);
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = kahead;
    kahead = aux;
}

void main_poll_register_fname(void (*desc)(struct pollfd*, uint32_t*), void (*serve)(struct pollfd*), const char* dname, const char* sname)
{
    pollentry* aux = (pollentry*)malloc(sizeof(pollentry));
    passert(aux);
    aux->desc = desc;
    aux->serve = serve;
    aux->dname = strdup(dname);
    aux->sname = strdup(sname);
    aux->next = pollhead;
    pollhead = aux;
}

void main_eachloop_register_fname(void (*fun)(void), const char* fname)
{
    eloopentry* aux = (eloopentry*)malloc(sizeof(eloopentry));
    passert(aux);
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = eloophead;
    eloophead = aux;
}

void main_chld_register_fname(pid_t pid, void (*fun)(int), const char* fname)
{
    chldentry* aux = (chldentry*)malloc(sizeof(chldentry));
    passert(aux);
    aux->pid = pid;
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = chldhead;
    chldhead = aux;
}

void* main_msectime_register_fname(uint32_t mseconds, uint32_t offset, void (*fun)(void), const char* fname)
{
    timeentry* aux;
    uint64_t useconds = UINT64_C(1000) * (uint64_t)mseconds;
    uint64_t usecoffset = UINT64_C(1000) * (uint64_t)offset;
    if (useconds == 0 || usecoffset >= useconds) {
        return NULL;
    }
    aux = (timeentry*)malloc(sizeof(timeentry));
    passert(aux);
    aux->nextevent = (((usecnow / useconds) * useconds) + usecoffset);
    while (aux->nextevent < usecnow) {
        aux->nextevent += useconds;
    }
    aux->useconds = useconds;
    aux->usecoffset = usecoffset;
    aux->fun = fun;
    aux->fname = strdup(fname);
    aux->next = timehead;
    timehead = aux;
    return aux;
}

int main_msectime_change(void* x, uint32_t mseconds, uint32_t offset)
{
    timeentry* aux = (timeentry*)x;
    uint64_t useconds = UINT64_C(1000) * (uint64_t)mseconds;
    uint64_t usecoffset = UINT64_C(1000) * (uint64_t)offset;
    if (useconds == 0 || usecoffset >= useconds) {
        return -1;
    }
    aux->nextevent = (((usecnow / useconds) * useconds) + usecoffset);
    while (aux->nextevent < usecnow) {
        aux->nextevent += useconds;
    }
    aux->useconds = useconds;
    aux->usecoffset = usecoffset;
    return 0;
}

void* main_time_register_fname(uint32_t seconds, uint32_t offset, void (*fun)(void), const char* fname)
{
    return main_msectime_register_fname(1000 * seconds, 1000 * offset, fun, fname);
}

int main_time_change(void* x, uint32_t seconds, uint32_t offset)
{
    return main_msectime_change(x, 1000 * seconds, 1000 * offset);
}

/* internal */

void free_all_registered_entries(void)
{
    deentry *de, *den;
    ceentry *ce, *cen;
    weentry *we, *wen;
    rlentry *re, *ren;
    inentry *ie, *ien;
    pollentry *pe, *pen;
    eloopentry *ee, *een;
    timeentry *te, *ten;

    for (de = dehead; de; de = den) {
        den = de->next;
        free(de->fname);
        free(de);
    }

    for (ce = cehead; ce; ce = cen) {
        cen = ce->next;
        free(ce->fname);
        free(ce);
    }

    for (we = wehead; we; we = wen) {
        wen = we->next;
        free(we->fname);
        free(we);
    }

    for (re = rlhead; re; re = ren) {
        ren = re->next;
        free(re->fname);
        free(re);
    }

    for (ie = inhead; ie; ie = ien) {
        ien = ie->next;
        free(ie->fname);
        free(ie);
    }

    for (pe = pollhead; pe; pe = pen) {
        pen = pe->next;
        free(pe->dname);
        free(pe->sname);
        free(pe);
    }

    for (ee = eloophead; ee; ee = een) {
        een = ee->next;
        free(ee->fname);
        free(ee);
    }

    for (te = timehead; te; te = ten) {
        ten = te->next;
        free(te->fname);
        free(te);
    }
}

int canexit(void)
{
    LOOP_VARS;
    int r;
    ceentry* aux;

    for (aux = cehead; aux != NULL; aux = aux->next) {
        LOOP_START;
        r = aux->fun();
        LOOP_END(aux->fname);
        if (r == 0) {
            return 0;
        }
    }
    return 1;
}

uint32_t main_time_refresh(void)
{
    struct timeval tv;
    uint32_t res;

    gettimeofday(&tv, NULL);
#ifdef USE_PTHREADS
    zassert(pthread_mutex_lock(&nowlock));
#endif
    res = now = tv.tv_sec;
#ifdef USE_PTHREADS
    zassert(pthread_mutex_unlock(&nowlock));
#endif
    return res;
}

uint32_t main_time(void)
{
#ifdef USE_PTHREADS
    uint32_t ret;
    zassert(pthread_mutex_lock(&nowlock));
    ret = now;
    zassert(pthread_mutex_unlock(&nowlock));
    return ret;
#else
    return now;
#endif
}

uint64_t main_utime(void)
{
    struct timeval tv;
    uint64_t usec;

    gettimeofday(&tv, NULL);
    usec = tv.tv_sec;
    usec *= 1000000;
    usec += tv.tv_usec;
    return usec;
}

static inline void destruct(void)
{
    LOOP_VARS;
    deentry* deit;

    for (deit = dehead; deit != NULL; deit = deit->next) {
        LOOP_START;
        deit->fun();
        LOOP_END(deit->fname);
    }
}

void main_keep_alive(void)
{
    LOOP_VARS;
    uint64_t useclast;
    struct timeval tv;
    kaentry* kait;

    gettimeofday(&tv, NULL);
    useclast = usecnow;
    usecnow = tv.tv_sec;
    usecnow *= 1000000;
    usecnow += tv.tv_usec;
#ifdef USE_PTHREADS
    zassert(pthread_mutex_lock(&nowlock));
#endif
    now = tv.tv_sec;
#ifdef USE_PTHREADS
    zassert(pthread_mutex_unlock(&nowlock));
#endif
    if (usecnow > useclast && useclast > 0) {
        useclast = usecnow - useclast;
    } else {
        useclast = 0;
    }
    if (useclast > 5000000) {
        syslog(LOG_WARNING, "long loop detected (%" PRIu64 ".%06" PRIu32 "s)", useclast / 1000000, (uint32_t)(useclast % 1000000));
    }

    for (kait = kahead; kait != NULL; kait = kait->next) {
        LOOP_START;
        kait->fun();
        LOOP_END(kait->fname);
    }
}

void mainloop()
{
    LOOP_VARS;
    uint64_t prevtime = 0;
    uint64_t useclast;
    struct timeval tv;
    pollentry* pollit;
    eloopentry* eloopit;
    timeentry* timeit;
    ceentry* ceit;
    weentry* weit;
    rlentry* rlit;
    inentry* init;
    struct pollfd pdesc[MFSMAXFILES];
    uint32_t ndesc;
    int i;
    int t, r;

    t = 0;
    r = 0;
    while (t != 3) {
        ndesc = 1;
        pdesc[0].fd = signalpipe[0];
        pdesc[0].events = POLLIN;
        pdesc[0].revents = 0;
        for (pollit = pollhead; pollit != NULL; pollit = pollit->next) {
            LOOP_START;
            pollit->desc(pdesc, &ndesc);
            LOOP_END(pollit->dname);
        }
        i = poll(pdesc, ndesc, 10);
        gettimeofday(&tv, NULL);
        useclast = usecnow;
        usecnow = tv.tv_sec;
        usecnow *= 1000000;
        usecnow += tv.tv_usec;
#ifdef USE_PTHREADS
        zassert(pthread_mutex_lock(&nowlock));
#endif
        now = tv.tv_sec;
#ifdef USE_PTHREADS
        zassert(pthread_mutex_unlock(&nowlock));
#endif
        if (usecnow > useclast && useclast > 0) {
            useclast = usecnow - useclast;
        } else {
            useclast = 0;
        }
        if (useclast > 5000000) {
            syslog(LOG_WARNING, "long loop detected (%" PRIu64 ".%06" PRIu32 "s)", useclast / 1000000, (uint32_t)(useclast % 1000000));
        }
        if (i < 0) {
            if (!ERRNO_ERROR) {
                syslog(LOG_WARNING, "poll returned EAGAIN");
                portable_usleep(10000);
                continue;
            }
            if (errno != EINTR) {
                syslog(LOG_WARNING, "poll error: %s", strerr(errno));
                break;
            }
        } else {
            if ((pdesc[0].revents) & POLLIN) {
                uint8_t sigid;
                if (read(signalpipe[0], &sigid, 1) == 1) {
                    if (sigid == '\001' && t == 0) {
                        set_quit_signal();
                        syslog(LOG_NOTICE, "terminate signal received");
                        t = 1;
                    } else if (sigid == '\002') {
                        syslog(LOG_NOTICE, "reloading config files");
                        r = 1;
                    } else if (sigid == '\003') {
                        syslog(LOG_NOTICE, "child finished");
                        r = 2;
                    } else if (sigid == '\004') {
                        syslog(LOG_NOTICE, "log extra info");
                        r = 3;
                    } else if (sigid == '\005') {
                        syslog(LOG_NOTICE, "unexpected alarm/prof signal received - ignoring");
                    } else if (sigid == '\006') {
                        syslog(LOG_NOTICE, "internal terminate request");
                        t = 1;
                    }
                }
            }
            for (pollit = pollhead; pollit != NULL; pollit = pollit->next) {
                LOOP_START;
                pollit->serve(pdesc);
                LOOP_END(pollit->sname);
            }
        }
        for (eloopit = eloophead; eloopit != NULL; eloopit = eloopit->next) {
            LOOP_START;
            eloopit->fun();
            LOOP_END(eloopit->fname);
        }
        if (usecnow < prevtime) {
            // time went backward !!! - recalculate "nextevent" time
            // adding previous_time_to_run prevents from running next event too soon.
            for (timeit = timehead; timeit != NULL; timeit = timeit->next) {
                uint64_t previous_time_to_run = timeit->nextevent - prevtime;
                if (previous_time_to_run > timeit->useconds) {
                    previous_time_to_run = timeit->useconds;
                }
                timeit->nextevent = ((usecnow / timeit->useconds) * timeit->useconds) + timeit->usecoffset;
                while (timeit->nextevent <= usecnow + previous_time_to_run) {
                    timeit->nextevent += timeit->useconds;
                }
            }
        } else if (usecnow > prevtime + UINT64_C(5000000)) {
            // time went forward !!! - just recalculate "nextevent" time
            for (timeit = timehead; timeit != NULL; timeit = timeit->next) {
                timeit->nextevent = ((usecnow / timeit->useconds) * timeit->useconds) + timeit->usecoffset;
                while (usecnow >= timeit->nextevent) {
                    timeit->nextevent += timeit->useconds;
                }
            }
        }
        for (timeit = timehead; timeit != NULL; timeit = timeit->next) {
            if (usecnow >= timeit->nextevent) {
                uint32_t eventcounter = 0;
                while (usecnow >= timeit->nextevent && eventcounter < 10) { // do not run more than 10 late entries
                    LOOP_START;
                    timeit->fun();
                    LOOP_END(timeit->fname);
                    timeit->nextevent += timeit->useconds;
                    eventcounter++;
                }
                if (usecnow >= timeit->nextevent) {
                    timeit->nextevent = ((usecnow / timeit->useconds) * timeit->useconds) + timeit->usecoffset;
                    while (usecnow >= timeit->nextevent) {
                        timeit->nextevent += timeit->useconds;
                    }
                }
            }
        }
        prevtime = usecnow;
        if (r == 1) {
            for (rlit = rlhead; rlit != NULL; rlit = rlit->next) {
                LOOP_START;
                rlit->fun();
                LOOP_END(rlit->fname);
            }
            r = 0;
        } else if (r == 2) {
            chldentry *chldit, **chldptr;
            pid_t pid;
            int status;

            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                chldptr = &chldhead;
                while ((chldit = *chldptr)) {
                    if (chldit->pid == pid) {
                        LOOP_START;
                        chldit->fun(status);
                        LOOP_END(chldit->fname);
                        *chldptr = chldit->next;
                        free(chldit);
                    } else {
                        chldptr = &(chldit->next);
                    }
                }
            }
            r = 0;
        } else if (r == 3) {
            for (init = inhead; init != NULL; init = init->next) {
                LOOP_START;
                init->fun();
                LOOP_END(init->fname);
            }
            r = 0;
        }
        if (t == 1) {
            for (weit = wehead; weit != NULL; weit = weit->next) {
                LOOP_START;
                weit->fun();
                LOOP_END(weit->fname);
            }
            t = 2;
        }
        if (t == 2) {
            i = 1;
            for (ceit = cehead; ceit != NULL && i; ceit = ceit->next) {
                LOOP_START;
                if (ceit->fun() == 0) {
                    i = 0;
                }
                LOOP_END(ceit->fname);
            }
            if (i) {
                t = 3;
            }
        }
    }
}

typedef int (*runfn)(void);
struct {
    runfn fn;
    char* name;
} RunTab[] = {
    { rnd_init, "random generator" },
    { hdd_init, "hdd space manager" },
    { mainserv_init, "main server threads" },
    { job_init, "jobs manager" },
    { csserv_init, "main server acceptor" }, /* it has to be before "masterconn" */
    { masterconn_init, "master connection module" },
    { (runfn)0, "****" }
},
  LateRunTab[] = { { hdd_late_init, "hdd space manager - threads" }, { (runfn)0, "****" } }, RestoreRunTab[] = { { hdd_restore, "hdd space restore" }, { (runfn)0, "****" } };

int initialize(void)
{
    uint32_t i;
    int ok;
    ok = 1;
    for (i = 0; (long int)(RunTab[i].fn) != 0 && ok; i++) {
#ifdef USE_PTHREADS
        zassert(pthread_mutex_lock(&nowlock));
#endif
        now = time(NULL);
#ifdef USE_PTHREADS
        zassert(pthread_mutex_unlock(&nowlock));
#endif
        if (RunTab[i].fn() < 0) {
            mfs_arg_syslog(LOG_ERR, "init: %s failed !!!", RunTab[i].name);
            ok = 0;
        }
    }
    return ok;
}

int restore(void)
{
    uint32_t i;
    int ok;
    ok = 1;
    for (i = 0; (long int)(RestoreRunTab[i].fn) != 0 && ok; i++) {
#ifdef USE_PTHREADS
        zassert(pthread_mutex_lock(&nowlock));
#endif
        now = time(NULL);
#ifdef USE_PTHREADS
        zassert(pthread_mutex_unlock(&nowlock));
#endif
        if (RestoreRunTab[i].fn() < 0) {
            mfs_arg_syslog(LOG_ERR, "restore: %s failed !!!", RestoreRunTab[i].name);
            ok = 0;
        }
    }
    return ok;
}

int initialize_late(void)
{
    uint32_t i;
    int ok;
    ok = 1;
    for (i = 0; (long int)(LateRunTab[i].fn) != 0 && ok; i++) {
#ifdef USE_PTHREADS
        zassert(pthread_mutex_lock(&nowlock));
#endif
        now = time(NULL);
#ifdef USE_PTHREADS
        zassert(pthread_mutex_unlock(&nowlock));
#endif
        if (LateRunTab[i].fn() < 0) {
            mfs_arg_syslog(LOG_ERR, "init: %s failed !!!", LateRunTab[i].name);
            ok = 0;
        }
    }
#ifdef USE_PTHREADS
    zassert(pthread_mutex_lock(&nowlock));
#endif
    now = time(NULL);
#ifdef USE_PTHREADS
    zassert(pthread_mutex_unlock(&nowlock));
#endif
    return ok;
}

/* signals */

static int termsignal[] = {
    SIGTERM,
    -1
};

static int reloadsignal[] = {
    SIGHUP,
    -1
};

static int infosignal[] = {
#ifdef SIGINFO
    SIGINFO,
#endif
#ifdef SIGUSR1
    SIGUSR1,
#endif
    -1
};

static int chldsignal[] = {
#ifdef SIGCHLD
    SIGCHLD,
#endif
#ifdef SIGCLD
    SIGCLD,
#endif
    -1
};

static int ignoresignal[] = {
    SIGQUIT,
#ifdef SIGPIPE
    SIGPIPE,
#endif
#ifdef SIGTSTP
    SIGTSTP,
#endif
#ifdef SIGTTIN
    SIGTTIN,
#endif
#ifdef SIGTTOU
    SIGTTOU,
#endif
#ifdef SIGUSR2
    SIGUSR2,
#endif
    -1
};

static int alarmsignal[] = {
#ifndef GPERFTOOLS
#ifdef SIGALRM
    SIGALRM,
#endif
#ifdef SIGVTALRM
    SIGVTALRM,
#endif
#ifdef SIGPROF
    SIGPROF,
#endif
#endif
    -1
};

static int daemonignoresignal[] = {
    SIGINT,
    -1
};

void termhandle(int signo)
{
    signo = write(signalpipe[1], "\001", 1); // killing two birds with one stone - use signo and do something with value returned by write :)
    (void)signo; // and then use this value to calm down compiler ;)
}

void reloadhandle(int signo)
{
    signo = write(signalpipe[1], "\002", 1); // see above
    (void)signo;
}

void chldhandle(int signo)
{
    signo = write(signalpipe[1], "\003", 1); // see above
    (void)signo;
}

void infohandle(int signo)
{
    signo = write(signalpipe[1], "\004", 1); // see above
    (void)signo;
}

void alarmhandle(int signo)
{
    signo = write(signalpipe[1], "\005", 1); // see above
    (void)signo;
}

void set_signal_handlers(int daemonflag)
{
    struct sigaction sa;
    uint32_t i;

    zassert(pipe(signalpipe));

#ifdef SA_RESTART
    sa.sa_flags = SA_RESTART;
#else
    sa.sa_flags = 0;
#endif
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = termhandle;
    for (i = 0; termsignal[i] > 0; i++) {
        sigaction(termsignal[i], &sa, (struct sigaction*)0);
    }
    sa.sa_handler = reloadhandle;
    for (i = 0; reloadsignal[i] > 0; i++) {
        sigaction(reloadsignal[i], &sa, (struct sigaction*)0);
    }
    sa.sa_handler = infohandle;
    for (i = 0; infosignal[i] > 0; i++) {
        sigaction(infosignal[i], &sa, (struct sigaction*)0);
    }
    sa.sa_handler = alarmhandle;
    for (i = 0; alarmsignal[i] > 0; i++) {
        sigaction(alarmsignal[i], &sa, (struct sigaction*)0);
    }
    sa.sa_handler = chldhandle;
    for (i = 0; chldsignal[i] > 0; i++) {
        sigaction(chldsignal[i], &sa, (struct sigaction*)0);
    }
    sa.sa_handler = SIG_IGN;
    for (i = 0; ignoresignal[i] > 0; i++) {
        sigaction(ignoresignal[i], &sa, (struct sigaction*)0);
    }
    sa.sa_handler = daemonflag ? SIG_IGN : termhandle;
    for (i = 0; daemonignoresignal[i] > 0; i++) {
        sigaction(daemonignoresignal[i], &sa, (struct sigaction*)0);
    }
}

void main_exit(void)
{
    int i;
    i = write(signalpipe[1], "\006", 1);
    (void)i;
}

void signal_cleanup(void)
{
    close(signalpipe[0]);
    close(signalpipe[1]);
}

static int lfd = -1; // main lock

pid_t mylock(int fd)
{
    struct flock fl;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = getpid();
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    for (;;) {
        if (fcntl(fd, F_SETLK, &fl) >= 0) { // lock set
            return 0; // ok
        }
        if (ERRNO_ERROR) { // error other than "already locked"
            return -1; // error
        }
        if (fcntl(fd, F_GETLK, &fl) < 0) { // get lock owner
            return -1; // error getting lock
        }
        if (fl.l_type != F_UNLCK) { // found lock
            return fl.l_pid; // return lock owner
        }
    }
    return -1; // pro forma
}

void wdunlock(void)
{
    if (lfd >= 0) {
        close(lfd);
    }
}

uint8_t wdlock(uint8_t runmode, uint32_t timeout)
{
    pid_t ownerpid;
    pid_t newownerpid;
    uint32_t l;

    lfd = open("." STR(APPNAME) ".lock", O_WRONLY | O_CREAT, 0666);
    if (lfd < 0) {
        mfs_errlog(LOG_ERR, "can't create lockfile in working directory");
        return 1;
    }
    ownerpid = mylock(lfd);
    if (ownerpid < 0) {
        mfs_errlog(LOG_ERR, "fcntl error");
        return 1;
    }
    if (ownerpid > 0) {
        if (runmode == RM_TEST) {
            fprintf(stderr, STR(APPNAME) " pid: %ld\n", (long)ownerpid);
            return 0;
        }
        if (runmode == RM_START) {
            fprintf(stderr, "can't start: lockfile is already locked by another process\n");
            return 1;
        }
        if (runmode == RM_RELOAD) {
            if (kill(ownerpid, SIGHUP) < 0) {
                mfs_errlog(LOG_WARNING, "can't send reload signal to lock owner");
                return 1;
            }
            fprintf(stderr, "reload signal has been sent\n");
            return 0;
        }
        if (runmode == RM_INFO) {
#if defined(SIGUSR1)
            if (kill(ownerpid, SIGUSR1) < 0) {
#elif defined(SIGINFO)
            if (kill(ownerpid, SIGINFO) < 0) {
#else
            if (1) {
#endif
                mfs_errlog(LOG_WARNING, "can't send info signal to lock owner");
                return 1;
            }
            fprintf(stderr, "info signal has been sent\n");
            return 0;
        }
        if (runmode == RM_KILL) {
            fprintf(stderr, "sending SIGKILL to lock owner (pid:%ld)\n", (long int)ownerpid);
            if (kill(ownerpid, SIGKILL) < 0) {
                mfs_errlog(LOG_WARNING, "can't kill lock owner");
                return 1;
            }
        } else {
            fprintf(stderr, "sending SIGTERM to lock owner (pid:%ld)\n", (long int)ownerpid);
            if (kill(ownerpid, SIGTERM) < 0) {
                mfs_errlog(LOG_WARNING, "can't kill lock owner");
                return 1;
            }
        }
        l = 0;
        fprintf(stderr, "waiting for termination ");
        fflush(stderr);
        do {
            newownerpid = mylock(lfd);
            if (newownerpid < 0) {
                mfs_errlog(LOG_ERR, "fcntl error");
                return 1;
            }
            if (newownerpid > 0) {
                l++;
                if (l >= timeout) {
                    syslog(LOG_ERR, "about %" PRIu32 " seconds passed and lockfile is still locked - giving up", l);
                    fprintf(stderr, ":giving up\n");
                    return 1;
                }
                if (l % 10 == 0) {
                    syslog(LOG_WARNING, "about %" PRIu32 " seconds passed and lock still exists", l);
                    fprintf(stderr, ".");
                    fflush(stderr);
                }
                if (newownerpid != ownerpid) {
                    fprintf(stderr, "\nnew lock owner detected\n");
                    if (runmode == RM_KILL) {
                        fprintf(stderr, ":sending SIGKILL to lock owner (pid:%ld):", (long int)newownerpid);
                        fflush(stderr);
                        if (kill(newownerpid, SIGKILL) < 0) {
                            mfs_errlog(LOG_WARNING, "can't kill lock owner");
                            return 1;
                        }
                    } else {
                        fprintf(stderr, ":sending SIGTERM to lock owner (pid:%ld):", (long int)newownerpid);
                        fflush(stderr);
                        if (kill(newownerpid, SIGTERM) < 0) {
                            mfs_errlog(LOG_WARNING, "can't kill lock owner");
                            return 1;
                        }
                    }
                    ownerpid = newownerpid;
                }
            }
            sleep(1);
        } while (newownerpid != 0);
        fprintf(stderr, "terminated\n");
        return 0;
    }
    if (runmode == RM_START || runmode == RM_RESTART) {
        char pidstr[20];
        l = snprintf(pidstr, 20, "%ld\n", (long)(getpid()));
        if (ftruncate(lfd, 0) < 0) {
            fprintf(stderr, "can't truncate pidfile\n");
        }
        if (write(lfd, pidstr, l) != (ssize_t)l) {
            fprintf(stderr, "can't write pid to pidfile\n");
        }
        fprintf(stderr, "lockfile created and locked\n");
    } else if (runmode == RM_TRY_RESTART) {
        fprintf(stderr, "can't find process to restart\n");
        return 1;
    } else if (runmode == RM_STOP || runmode == RM_KILL) {
        fprintf(stderr, "can't find process to terminate\n");
        return 0;
    } else if (runmode == RM_RELOAD) {
        fprintf(stderr, "can't find process to send reload signal\n");
        return 1;
    } else if (runmode == RM_INFO) {
        fprintf(stderr, "can't find process to send info signal\n");
        return 1;
    } else if (runmode == RM_TEST) {
        fprintf(stderr, STR(APPNAME) " is not running\n");
        return 1;
    }
    return 0;
}

void createpath(const char* filename)
{
    char pathbuff[1024];
    const char* src = filename;
    char* dst = pathbuff;
    if (*src == '/')
        *dst++ = *src++;

    while (*src) {
        while (*src != '/' && *src) {
            *dst++ = *src++;
        }
        if (*src == '/') {
            *dst = '\0';
            if (mkdir(pathbuff, (mode_t)0777) < 0) {
                if (errno != EEXIST) {
                    mfs_arg_errlog(LOG_NOTICE, "creating directory %s", pathbuff);
                }
            } else {
                mfs_arg_syslog(LOG_NOTICE, "directory %s has been created", pathbuff);
            }
            *dst++ = *src++;
        }
    }
}

void mfschunkserver(void)
{
    struct node_identity localnode = get_local_node();

    int ch;
    int lockmemory = 0;
    int forcecoredump = 1;
    uint32_t locktimeout = 1800;
    struct rlimit rls;
#if defined(USE_PTHREADS) && defined(M_ARENA_MAX) && defined(M_ARENA_TEST) && defined(HAVE_MALLOPT)
    uint32_t limit_glibc_arenas;
#endif
    strerr_init();
    mycrc32_init();
    set_signal_handlers(0);
    // processname_init(0, NULL);
    char* logappname = strdup(localnode.syslogident);

    // setup logging
    if (logappname[0]) {
        openlog(logappname, LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_USER);
    } else {
        openlog(STR(APPNAME), LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_USER);
    }

    // set open file limits
    rls.rlim_cur = MFSMAXFILES;
    rls.rlim_max = MFSMAXFILES;
    if (setrlimit(RLIMIT_NOFILE, &rls) < 0) {
        syslog(LOG_NOTICE, "can't change open files limit to: %u (trying to set smaller value)", (unsigned)(MFSMAXFILES));
        if (getrlimit(RLIMIT_NOFILE, &rls) >= 0) {
            uint32_t limit;
            if (rls.rlim_max > MFSMAXFILES) {
                limit = MFSMAXFILES;
            } else {
                limit = rls.rlim_max;
            }
            while (limit > 1024) {
                rls.rlim_cur = limit;
                if (setrlimit(RLIMIT_NOFILE, &rls) >= 0) {
                    mfs_arg_syslog(LOG_NOTICE, "open files limit has been set to: %" PRIu32, limit);
                    break;
                }
                limit *= 3;
                limit /= 4;
            }
        }
    } else {
        mfs_arg_syslog(LOG_NOTICE, "open files limit has been set to: %u", (unsigned)(MFSMAXFILES));
    }

    // set thread priority
    lockmemory = LOCK_MEMORY ? 1 : 0;
    int32_t nicelevel = NICE_LEVEL;
    setpriority(PRIO_PROCESS, getpid(), nicelevel);

    // set working directory
    char* wrkdir = strdup(localnode.datapath);
    fprintf(stderr, "working directory: %s\n", wrkdir);
    if (chdir(wrkdir) < 0) {
        mfs_arg_syslog(LOG_ERR, "can't set working directory to %s", wrkdir);
        closelog();
        free(logappname);
        return;
    }
    free(wrkdir);
    umask(FILE_UMASK & 0x077);
    ch = wdlock(RM_START, locktimeout);
    if (ch) {
        signal_cleanup();
        strerr_term();
        wdunlock();
        closelog();
        free(logappname);
        return;
    }

#ifdef MFS_USE_MEMLOCK
    if (lockmemory) {
        if (getrlimit(RLIMIT_MEMLOCK, &rls) < 0) {
            mfs_errlog(LOG_WARNING, "error getting memory lock limits");
        } else {
            if (rls.rlim_cur != RLIM_INFINITY && rls.rlim_max == RLIM_INFINITY) {
                rls.rlim_cur = RLIM_INFINITY;
                rls.rlim_max = RLIM_INFINITY;
                if (setrlimit(RLIMIT_MEMLOCK, &rls) < 0) {
                    mfs_errlog(LOG_WARNING, "error setting memory lock limit to unlimited");
                }
            }
            if (getrlimit(RLIMIT_MEMLOCK, &rls) < 0) {
                mfs_errlog(LOG_WARNING, "error getting memory lock limits");
            } else {
                if (rls.rlim_cur != RLIM_INFINITY) {
                    mfs_errlog(LOG_WARNING, "can't set memory lock limit to unlimited");
                } else {
                    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
                        mfs_errlog(LOG_WARNING, "memory lock error");
                    } else {
                        mfs_syslog(LOG_NOTICE, "process memory was successfully locked in RAM");
                    }
                }
            }
        }
    }
#else
    if (lockmemory) {
        mfs_syslog(LOG_WARNING, "memory lock not supported !!!");
    }
#endif

    if (forcecoredump) {
        rls.rlim_cur = RLIM_INFINITY;
        rls.rlim_max = RLIM_INFINITY;
        setrlimit(RLIMIT_CORE, &rls);
#if defined(HAVE_PRCTL) && defined(PR_SET_DUMPABLE)
        prctl(PR_SET_DUMPABLE, 1);
#endif
    }

/* glibc malloc tuning */
#if defined(USE_PTHREADS) && defined(M_ARENA_MAX) && defined(M_ARENA_TEST) && defined(HAVE_MALLOPT)
    limit_glibc_arenas = LIMIT_GLIBC_MALLOC_ARENAS;
    if (limit_glibc_arenas) {
        if (!getenv("MALLOC_ARENA_MAX")) {
            mfs_arg_syslog(LOG_NOTICE, "setting glibc malloc arena max to %" PRIu32, limit_glibc_arenas);
            mallopt(M_ARENA_MAX, limit_glibc_arenas);
        }
        if (!getenv("MALLOC_ARENA_TEST")) {
            mfs_arg_syslog(LOG_NOTICE, "setting glibc malloc arena test to %" PRIu32, limit_glibc_arenas);
            mallopt(M_ARENA_TEST, limit_glibc_arenas);
        }
    } else {
        mfs_syslog(LOG_NOTICE, "setting glibc malloc arenas turned off");
    }
#endif /* glibc malloc tuning */

#if defined(__linux__) && defined(OOM_ADJUSTABLE)
    if (DISABLE_OOM_KILLER) {
        FILE* oomfd;
        int oomdis;
        oomdis = 0;
#if defined(OOM_SCORE_ADJ_MIN)
        oomfd = fopen("/proc/self/oom_score_adj", "w");
        if (oomfd != NULL) {
            fprintf(oomfd, "%d\n", OOM_SCORE_ADJ_MIN);
            fclose(oomfd);
            oomdis = 1;
#if defined(OOM_DISABLE)
        } else {
            oomfd = fopen("/proc/self/oom_adj", "w");
            if (oomfd != NULL) {
                fprintf(oomfd, "%d\n", OOM_DISABLE);
                fclose(oomfd);
                oomdis = 1;
            }
#endif
        }
#elif defined(OOM_DISABLE)
        oomfd = fopen("/proc/self/oom_adj", "w");
        if (oomfd != NULL) {
            fprintf(oomfd, "%d\n", OOM_DISABLE);
            fclose(oomfd);
            oomdis = 1;
        }
#endif
        if (oomdis) {
            syslog(LOG_NOTICE, "out of memory killer disabled");
        } else {
            syslog(LOG_NOTICE, "can't disable out of memory killer");
        }
    }
#endif

    syslog(LOG_NOTICE, "monotonic clock function: %s", monotonic_method());
    syslog(LOG_NOTICE, "monotonic clock speed: %" PRIu32 " ops / 10 ms", monotonic_speed());

    fprintf(stderr, "initializing %s modules ...\n", logappname);

    if (initialize()) {
        fprintf(stderr, "%s daemon initialized properly\n", logappname);
        if (initialize_late()) {
            mainloop();
            mfs_syslog(LOG_NOTICE, "exited from main loop");
            ch = 0;
        } else {
            ch = 1;
        }
    } else {
        fprintf(stderr, "error occurred during initialization - exiting\n");
        ch = 1;
    }

    mfs_syslog(LOG_NOTICE, "exiting ...");
    destruct();
    free_all_registered_entries();
    signal_cleanup();
    strerr_term();
    wdunlock();
    mfs_arg_syslog(LOG_NOTICE, "process exited successfully (status:%d)", ch);
    closelog();
    free(logappname);
    return;
}
