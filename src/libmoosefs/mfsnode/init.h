// Copyright (c) 2022 datosdrive
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MFSNODE_INIT_H
#define MFSNODE_INIT_H

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mutex>
#include <thread>

#include "mfscommon/mainthread.h"

#define VERSION_MAJ 3
#define VERSION_MID 0
#define VERSION_MIN 116
#define VERSSTR "libmoosefs"

extern struct node_identity node_info;
const uint32_t get_node_version();

struct
node_identity {
    bool ready = false;
    char hostname[256];
    char masterhost[256];
    char masterport[256];
    char bindhost[256];
    char listenhost[256];
    char listenport[256];
    char syslogident[256];
    char basepath[128];
    char datapath[256];
    char chunkpath[256];
};

bool get_quit_signal();
void set_quit_signal();

struct node_identity get_local_node();
bool set_local_node(bool net_type);

#endif // MFSNODE_INIT_H
