// Copyright (c) 2023 pacprotocol/barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STORAGE_REWARDS_H
#define STORAGE_REWARDS_H

#include <amount.h>
#include <evo/deterministicmns.h>
#include <storage/behavior.h>
#include <validation.h>

#include <stdint.h>
#include <stdio.h>

CAmount GetBaseReward();
CAmount CalculateNodeReward(CAmount& base_reward, int space_mode, int score);
CAmount GetMasternodePayment(int nHeight);

#endif // STORAGE_REWARDS_H
