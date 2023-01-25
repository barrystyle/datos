// Copyright (c) 2023 pacprotocol/barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <storage/util.h>

void uint32_to_ip(uint32_t address, char* straddress)
{
    unsigned char bytes[4];
    bytes[0] = address & 0xFF;
    bytes[1] = (address >> 8) & 0xFF;
    bytes[2] = (address >> 16) & 0xFF;
    bytes[3] = (address >> 24) & 0xFF;
    snprintf(straddress, 16, "%u.%u.%u.%u", bytes[3], bytes[2], bytes[1], bytes[0]);
}
