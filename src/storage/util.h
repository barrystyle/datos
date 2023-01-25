// Copyright (c) 2023 datosdrive/barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STORAGE_UTIL_H
#define STORAGE_UTIL_H

#include <stdint.h>
#include <stdio.h>

#include <list>

void uint32_to_ip(uint32_t address, char* straddress, int port = 0);

#endif // STORAGE_UTIL_H
