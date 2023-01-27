// Copyright (c) 2022 datosdrive
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef POS_STAKEMAN_H
#define POS_STAKEMAN_H

#include <logging.h>
#include <pos/minter.h>
#include <shutdown.h>
#include <util/time.h>

void stakeman_request_start();
void stakeman_request_stop();
void stakeman_handler();

#endif // POS_STAKEMAN_H
