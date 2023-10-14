// Copyright (c) 2023 datos
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <storage/rewards.h>

CAmount GetBaseReward()
{
    return 8824 * COIN;
}

CAmount CalculateNodeReward(CAmount& base_reward, int space_mode, int score)
{
    int percentile;
    CAmount calcReward;

    // return zero if params are out of range
    if (space_mode < 1 || space_mode > 10) {
        LogPrint(BCLog::STORAGE, "%s: returning 0 because space_mode out of range\n", __func__);
        return 0 * COIN;
    }

    // debug out
    LogPrint(BCLog::STORAGE, "%s: called with base_reward %ld, space_mode %d and score %d\n", __func__, base_reward, space_mode, score);

    switch (space_mode) {
        case 1: percentile = 5;
        case 2: percentile = 6;
        case 3: percentile = 7;
        case 4: percentile = 8;
        case 5: percentile = 9;
        case 6: percentile = 10;
        case 7: percentile = 11;
        case 8: percentile = 12;
        case 9: percentile = 15;
        case 10: percentile = 17;
    }

    calcReward = base_reward * percentile / 100;
    if (score < 100) {
        calcReward = 0 * COIN;
    }

    return calcReward;
}

int ConvertActualSpaceToSpaceMode(int actual_space)
{
    return actual_space / 25;
}

CAmount GetMasternodePayment(int nHeight)
{
    const CBlockIndex* pindex = WITH_LOCK(cs_main, return ::ChainActive()[nHeight - 1]);
    auto dmnPayee = deterministicMNManager->GetListForBlock(pindex).GetMNPayee();
    if (!dmnPayee) {
        return 0;
    }

    // get storagenode
    int score, space;
    CService mnAddress(dmnPayee->pdmnState->addr);
    scoreManager.GetNodeScore(mnAddress, score, space);

    // convert from actual space to space_mode
    int space_mode = ConvertActualSpaceToSpaceMode(space);

    // calculate amount
    CAmount base_reward = GetBaseReward();
    CAmount reward = CalculateNodeReward(base_reward, space_mode, score);

    return reward;
}
