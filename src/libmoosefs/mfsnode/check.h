// Copyright (c) 2022 pacprotocol
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MFSNODE_CHECK_H
#define MFSNODE_CHECK_H

#define MODE_FAST 1
#define MODE_EMPTY 2
#define MODE_NAME 4
#define MODE_REPAIR 8

int chunk_repair(const char *fname, uint8_t mode, uint8_t showok);

#endif // MFSNODE_CHECK_H
