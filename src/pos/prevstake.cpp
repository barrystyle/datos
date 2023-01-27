// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2022 datosdrive
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/prevstake.h>

CCriticalSection cs_stakeseen;
std::list<COutPoint> listStakeSeen;
std::map<COutPoint, uint256> mapStakeSeen;
static const size_t MAX_STAKE_SEEN_SIZE = 1000;

bool AddToMapStakeSeen(const COutPoint &kernel, const uint256 &blockHash)
{
    LOCK(cs_stakeseen);

    std::pair<std::map<COutPoint, uint256>::iterator,bool> ret;
    ret = mapStakeSeen.insert(std::pair<COutPoint, uint256>(kernel, blockHash));
    if (ret.second == false) {
        ret.first->second = blockHash;
    } else {
        listStakeSeen.push_back(kernel);
    }

    return true;
};

bool SearchStakeNotSeen(const COutPoint &kernel, uint256& hash)
{
    LOCK(cs_stakeseen);

    std::map<COutPoint, uint256>::const_iterator mi = mapStakeSeen.find(kernel);
    if (mi == mapStakeSeen.end()) {
        hash = uint256();
        return true;
    }
    hash = mi->second;

    return false;
}

void MaintainSeenCache()
{
    LOCK(cs_stakeseen);

    while (listStakeSeen.size() > MAX_STAKE_SEEN_SIZE) {
        const COutPoint &oldest = listStakeSeen.front();
        if (!mapStakeSeen.erase(oldest)) {
            LogPrint(BCLog::POS, "%s: Warning: mapStakeSeen did not erase %s %n\n", __func__, oldest.hash.ToString(), oldest.n);
        }
        listStakeSeen.pop_front();
    }
}

bool CheckStakeUnused(const COutPoint &kernel)
{
    uint256 hash;
    return SearchStakeNotSeen(kernel, hash);
}

bool CheckStakeUnique(const CBlock &block, bool fUpdate)
{
    uint256 blockHash = block.GetHash();
    const COutPoint &kernel = block.vtx[1]->vin[1].prevout;

    uint256 hash;
    if (!SearchStakeNotSeen(kernel, hash)) {
        if (hash == blockHash) {
            return true;
        }
        LogPrint(BCLog::POS, "%s: Stake kernel for %s first seen on %s\n", __func__, blockHash.ToString(), hash.ToString());
        return false;
    }

    if (!fUpdate) {
        return true;
    }

    MaintainSeenCache();

    return AddToMapStakeSeen(kernel, blockHash);
};

