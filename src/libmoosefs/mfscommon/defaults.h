// Copyright (c) 2023 datos
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MFSCOMMON_DEFAULTS_H
#define MFSCOMMON_DEFAULTS_H

#include <stdint.h>
#include <stdbool.h>

const bool DISABLE_OOM_KILLER = true;
const bool LOCK_MEMORY = false;
const double HDD_TEST_SPEED = 1.0;
const int32_t HDD_FADVISE_MIN_TIME = 86400;
const int32_t HDD_KEEP_DUPLICATES_HOURS = 168;
const int32_t HDD_RR_CHUNK_COUNT = 10000;
const int32_t NICE_LEVEL = -19;
const uint32_t CHUNKS_PER_REGISTER_PACKET = 10000;
const uint32_t FILE_UMASK = 0x027;
const uint32_t HDD_ERROR_TOLERANCE_COUNT = 2;
const uint32_t HDD_ERROR_TOLERANCE_PERIOD = 600;
const uint32_t HDD_HIGH_SPEED_REBALANCE_LIMIT = 0;
const uint32_t HDD_LEAVE_SPACE_DEFAULT = 0x40000000;
const uint32_t HDD_MIN_TEST_INTERVAL = 86400;
const uint32_t HDD_REBALANCE_UTILIZATION = 20;
const uint32_t MASTER_RECONNECTION_DELAY = 2;
const uint32_t MASTER_TIMEOUT = 0;
const uint32_t WORKERS_MAX = 250;
const uint32_t WORKERS_MAX_IDLE = 40;
const uint8_t HDD_FSYNC_BEFORE_CLOSE = 0;
const uint8_t HDD_SPARSIFY_ON_WRITE = 1;
const uint8_t LIMIT_GLIBC_MALLOC_ARENAS = 4;

#endif // MFSCOMMON_DEFAULTS_H
