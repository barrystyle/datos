// Copyright (c) 2017-2019 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_POS_MINTER_H
#define PARTICL_POS_MINTER_H

#include <thread>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <string>

class CWallet;
class CBlock;
class CStakeWallet;
extern CStakeWallet wallet;

class StakeThread
{
public:
    void condWaitFor(int ms);

    StakeThread() {};
    std::thread thread;
    std::condition_variable condMinerProc;
    std::mutex mtxMinerProc;
    std::string sName;
    bool fWakeMinerProc = false;
};

extern std::vector<StakeThread*> vStakeThreads;

extern std::atomic<bool> fIsStaking;

extern int nMinStakeInterval;
extern int nMinerSleep;

bool CheckStake(CBlock *pblock);

void StartThreadStakeMiner();
void StopThreadStakeMiner();
void WakeThreadStakeMiner(CWallet *pwallet);
bool ThreadStakeMinerStopped(); // replace interruption_point
void ThreadStakeMiner(size_t nThreadID, CWallet *pwallet);

#endif // PARTICL_POS_MINTER_H

