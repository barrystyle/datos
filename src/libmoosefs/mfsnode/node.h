// Copyright (c) 2022 barrystyle/datosdrive
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MFSNODE_NODE_H
#define MFSNODE_NODE_H

#include "mfscommon/crc.h"
#include "mfsnode/init.h"
#include "mfsnode/check.h"
#include "mfschunkserver/hddspacemgr.h"

#include <dirent.h>

#include <string>
#include <thread>

extern std::thread chunk_thread;

void launch_chunkserver(int space_mode, bool net_type);

#endif // MFSNODE_NODE_H
