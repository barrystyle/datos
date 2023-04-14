// Copyright (c) 2022 barrystyle/datosdrive
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mfsnode/node.h"

bool read_chunkfolder(char* chunkfolder)
{
    mycrc32_init();
    DIR* dr = opendir(chunkfolder);
    // chunkfolder must exist
    if (dr) {
        for (unsigned int i = 0; i < 256; i++) {
            char chunkpath[256];
            memset(chunkpath, 0, sizeof(chunkpath));
            sprintf(chunkpath, "%s/%02hhX", chunkfolder, i);
            // subfolders might exist
            struct dirent* en;
            DIR* subdir = opendir(chunkpath);
            if (subdir) {
                // iterate chunk files
                while ((en = readdir(subdir)) != NULL) {
                    if (!strcmp(en->d_name, ".") || !strcmp(en->d_name, ".."))
                        continue;
                    // build chunkfile
                    int file_result;
                    char chunkfile[256];
                    memset(chunkfile, 0, sizeof(chunkfile));
                    sprintf(chunkfile, "%s/%s", chunkpath, en->d_name);
                    file_result = chunk_repair(chunkfile, MODE_FAST, true);
                    if (file_result != 0)
                        return false;
                }
                closedir(subdir);
            }
        }
        return true;
    }
    return false;
}

void launch_chunkserver(int space_mode, bool net_type)
{
    if (!set_local_node(net_type)) {
        printf("error provisioning storage node..\n");
        return;
    }

    chunkservconf.set_target_space(space_mode);

    printf("allocated:  %dGiB\n", chunkservconf.get_target_space());
    printf("basepath:   %s\n", node_info.basepath);
    printf("datapath:   %s\n", node_info.datapath);
    printf("chunkspath: %s\n", node_info.chunkpath);

    // halt chunkserver thread
    if (!read_chunkfolder(node_info.chunkpath)) {
        printf("error found whilst checking chunk files..\n");
        return;
    }

    std::thread chunk_thread(mfschunkserver);

    while (true) {
        usleep(100000);
        if (get_quit_signal()) break;
    }

    if (chunk_thread.joinable()) chunk_thread.join();

    return;
}
