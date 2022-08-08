// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2022 pacprotocol
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/stakeseen.h>

std::list<COutPoint> listStakeSeen;
std::map<COutPoint, uint256> mapStakeSeen;
static const size_t MAX_STAKE_SEEN_SIZE = 1000;

bool AddToMapStakeSeen(const COutPoint &kernel, const uint256 &blockHash)
{
    std::pair<std::map<COutPoint, uint256>::iterator,bool> ret;
    ret = mapStakeSeen.insert(std::pair<COutPoint, uint256>(kernel, blockHash));
    if (ret.second == false) {
        ret.first->second = blockHash;
    } else {
        listStakeSeen.push_back(kernel);
    }

    return true;
};

bool CheckStakeUnused(const COutPoint &kernel)
{
    std::map<COutPoint, uint256>::const_iterator mi = mapStakeSeen.find(kernel);
    return (mi == mapStakeSeen.end());
}

bool CheckStakeUnique(const CBlock &block, bool fUpdate)
{
    LOCK(cs_main);

    uint256 blockHash = block.GetHash();
    const COutPoint &kernel = block.vtx[1]->vin[1].prevout;

    std::map<COutPoint, uint256>::const_iterator mi = mapStakeSeen.find(kernel);
    if (mi != mapStakeSeen.end()) {
        if (mi->second == blockHash) {
            return true;
        }
        return error("%s: Stake kernel for %s first seen on %s.", __func__, blockHash.ToString(), mi->second.ToString());
    }

    if (!fUpdate) {
        return true;
    }

    while (listStakeSeen.size() > MAX_STAKE_SEEN_SIZE) {
        const COutPoint &oldest = listStakeSeen.front();
        if (1 != mapStakeSeen.erase(oldest)) {
            LogPrint(BCLog::POS, "%s: Warning: mapStakeSeen did not erase %s %n\n", __func__, oldest.hash.ToString(), oldest.n);
        }
        listStakeSeen.pop_front();
    }

    return AddToMapStakeSeen(kernel, blockHash);
};

