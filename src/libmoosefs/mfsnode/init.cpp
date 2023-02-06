// Copyright (c) 2022 barrystyle/datosdrive
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mfsnode/init.h"

struct node_identity node_info;

bool quit_flag = false;

bool get_quit_signal() {
    return quit_flag;
}

void set_quit_signal() {
    quit_flag = true;
}

struct node_identity get_local_node()
{
    return node_info;
}

const uint32_t get_node_version()
{
    return (uint32_t)((VERSION_MAJ)*0x10000 + (VERSION_MID)*0x100 + (VERSION_MIN * 0x2));
}

char* get_storagedir(bool net_type)
{
#ifdef __linux__
    char homedir[256];
    memset(homedir, 0, sizeof(homedir));
    if (net_type)
        snprintf(homedir, 256, "%s/.datosstorage", getenv("HOME"));
    else
        snprintf(homedir, 256, "%s/.datosteststorage", getenv("HOME"));
    return strdup(homedir);
#else
    return NULL;
#endif
}

void create_storagedir(char* extrapath, bool net_type)
{
    char* path = get_storagedir(net_type);
    if (!path)
        return;
    if (!extrapath) {
        mkdir(path, 0755);
    } else {
        char fullpath[256];
        memset(fullpath, 0, sizeof(fullpath));
        sprintf(fullpath, "%s/%s", path, extrapath);
        mkdir(fullpath, 0755);
    }
}

bool set_local_node(bool net_type)
{
    if (net_type) {
        node_info.masterhost.push_back("mfsmain0.datosdrive.com");
        node_info.masterhost.push_back("mfsmain1.datosdrive.com");
        node_info.masterhost.push_back("mfsmain2.datosdrive.com");
    } else {
        node_info.masterhost.push_back("mfstest0.datosdrive.com");
        node_info.masterhost.push_back("mfstest1.datosdrive.com");
        node_info.masterhost.push_back("mfstest2.datosdrive.com");
    }

    sprintf(node_info.masterport, "9420");
    sprintf(node_info.bindhost, "*");
    sprintf(node_info.listenhost, "*");
    sprintf(node_info.listenport, "11012");
    sprintf(node_info.syslogident, "libmoosefs");

    if (!get_storagedir(net_type)) {
        return false;
    }

    sprintf(node_info.basepath, "%s", get_storagedir(net_type));
    create_storagedir(NULL, net_type);
    sprintf(node_info.datapath, "%s/data", node_info.basepath);
    create_storagedir("data", net_type);
    sprintf(node_info.chunkpath, "%s/chunks", node_info.basepath);
    create_storagedir("chunks", net_type);

    return true;
}
