// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2022 pacprotocol
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/minter.h>

#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <masternode/sync.h>
#include <miner.h>
#include <pos/kernel.h>
#include <pos/signature.h>
#include <pos/wallet.h>
#include <pos/prevstake.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <shutdown.h>
#include <sync.h>
#include <net.h>
#include <util/moneystr.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <stdint.h>

CStakeWallet wallet;
std::vector<StakeThread*> vStakeThreads;

void StakeThread::condWaitFor(int ms)
{
    std::unique_lock<std::mutex> lock(mtxMinerProc);
    fWakeMinerProc = false;
    condMinerProc.wait_for(lock, std::chrono::milliseconds(ms), [this] { return this->fWakeMinerProc; });
};

std::atomic<bool> fStopMinerProc(false);
std::atomic<bool> fTryToSync(false);
std::atomic<bool> fIsStaking(false);

int nMinStakeInterval = 0;  // min stake interval in seconds
int nMinerSleep = 500;
std::atomic<int64_t> nTimeLastStake(0);

extern double GetDifficulty(const CBlockIndex* blockindex = nullptr);

bool CheckStake(CBlock *pblock)
{
    uint256 proofHash, hashTarget;
    uint256 hashBlock = pblock->GetHash();
    const Consensus::Params& params = Params().GetConsensus();

    if (!pblock->IsProofOfStake()) {
        return error("%s: %s is not a proof-of-stake block.", __func__, hashBlock.GetHex());
    }

    if (!CheckStakeUnique(*pblock, false)) {
        return error("%s: %s CheckStakeUnique failed.", __func__, hashBlock.GetHex());
    }

    {
        BlockMap::const_iterator mi = BlockIndex().find(pblock->hashPrevBlock);
        if (mi == BlockIndex().end()) {
            return error("%s: %s prev block not found: %s.", __func__, hashBlock.GetHex(), pblock->hashPrevBlock.GetHex());
        }

        if (!::ChainActive().Contains(mi->second)) {
            return error("%s: %s prev block in active chain: %s.", __func__, hashBlock.GetHex(), pblock->hashPrevBlock.GetHex());
        }

        CValidationState state;
        if (!CheckProofOfStake(state, mi->second, *pblock->vtx[1], pblock->nTime, pblock->nBits, proofHash, hashTarget, params)) {
            return error("%s: proof-of-stake checking failed.", __func__);
        }

        if (pblock->hashPrevBlock != ::ChainActive().Tip()->GetBlockHash()) {
            return error("%s: Generated block is stale.", __func__);
        }
    }

    LogPrint(BCLog::POS, "%s: New proof-of-stake block found  \n  hash: %s \nproofhash: %s  \ntarget: %s\n", __func__, hashBlock.GetHex(), proofHash.GetHex(), hashTarget.GetHex());

    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr)) {
        return error("%s: Block not accepted.", __func__);
    }

    return true;
};

void StartThreadStakeMiner()
{
    nMinStakeInterval = gArgs.GetArg("-minstakeinterval", 0);
    nMinerSleep = gArgs.GetArg("-minersleep", 500);

    size_t i = 0;
    StakeThread *t = new StakeThread();
    t->sName = strprintf("miner%d", i);
    t->thread = std::thread(std::bind(&ThreadStakeMiner, i, GetWallets().front().get()));
    vStakeThreads.push_back(t);

    fStopMinerProc = false;
};

void StopThreadStakeMiner()
{
    if (vStakeThreads.size() < 1 // no thread created
        || fStopMinerProc) {
        return;
    }
    LogPrint(BCLog::POS, "StopThreadStakeMiner\n");
    fStopMinerProc = true;
    fIsStaking = false;
    fTryToSync = false;

    for (auto t : vStakeThreads) {
        {
            std::lock_guard<std::mutex> lock(t->mtxMinerProc);
            t->fWakeMinerProc = true;
        }
        t->condMinerProc.notify_all();
        t->thread.join();
        delete t;
    }

    vStakeThreads.clear();
};

void WakeThreadStakeMiner(CWallet *pwallet)
{
    // Call when chain is synced, wallet unlocked or balance changed
    LOCK(pwallet->cs_wallet);
    LogPrint(BCLog::POS, "WakeThreadStakeMiner thread %d\n", pwallet->nStakeThread);

    if (pwallet->nStakeThread >= vStakeThreads.size()) {
        return; // stake unit test
    }
    StakeThread *t = vStakeThreads[pwallet->nStakeThread];
    pwallet->nLastCoinStakeSearchTime = 0;
    {
        std::lock_guard<std::mutex> lock(t->mtxMinerProc);
        t->fWakeMinerProc = true;
    }

    t->condMinerProc.notify_all();
};

bool ThreadStakeMinerStopped()
{
    return fStopMinerProc;
}

static inline void condWaitFor(size_t nThreadID, int ms)
{
    assert(vStakeThreads.size() > nThreadID);
    StakeThread *t = vStakeThreads[nThreadID];
    t->condWaitFor(ms);
};

void ThreadStakeMiner(size_t nThreadID, CWallet* pwallet)
{
    LogPrint(BCLog::POS, "Starting staking thread %d.\n", nThreadID);

    int nBestHeight;
    int64_t nBestTime;

    const CChainParams& params = Params();
    int min_nodes = params.NetworkIDString() == "regtest" ? 0 : 3;

    // initialize stakewallet instance
    wallet.SetParams();
    wallet.AttachWallet(pwallet);

    CScript coinbaseScript;
    while (!fStopMinerProc)
    {
        UninterruptibleSleep(std::chrono::milliseconds{150});

        int num_nodes;
        {
            LOCK(cs_main);
            nBestHeight = ::ChainActive().Height();
            nBestTime = ::ChainActive().Tip()->nTime;
            num_nodes = g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL);
        }

        if (fTryToSync) {
            fTryToSync = false;
            if (num_nodes < min_nodes || ::ChainstateActive().IsInitialBlockDownload()) {
                fIsStaking = false;
                LogPrint(BCLog::POS, "%s: TryToSync\n", __func__);
                condWaitFor(nThreadID, 15000);
                continue;
            }
        }

        if (num_nodes < min_nodes || ::ChainstateActive().IsInitialBlockDownload()) {
            fIsStaking = false;
            fTryToSync = true;
            LogPrint(BCLog::POS, "%s: IsInitialBlockDownload\n", __func__);
            condWaitFor(nThreadID, 15000);
            continue;
        }

        if (nBestHeight < params.GetConsensus().nLastPoWBlock) {
            fIsStaking = false;
            LogPrint(BCLog::POS, "%s: WaitForPoS\n", __func__);
            condWaitFor(nThreadID, 15000);
            continue;
        }

        if (!masternodeSync.IsSynced()) {
            fIsStaking = false;
            fTryToSync = true;
            LogPrint(BCLog::POS, "%s: IsMasternodeSynced\n", __func__);
            condWaitFor(nThreadID, 15000);
            continue;
        }

        int64_t nTime = GetAdjustedTime();
        int64_t nMask = GetStakeTimestampMask(nBestHeight+1);
        int64_t nSearchTime = nTime & ~nMask;

        if (nSearchTime <= nBestTime)
        {
            if (nTime < nBestTime) {
                LogPrint(BCLog::POS, "%s: Can't stake before last block time.\n", __func__);
                condWaitFor(nThreadID, std::min(1000 + (nBestTime - nTime) * 1000, (int64_t)30000));
                continue;
            }

            int64_t nNextSearch = nSearchTime + nMask;
            condWaitFor(nThreadID, std::min(nMinerSleep + (nNextSearch - nTime) * 1000, (int64_t)10000));
            continue;
        }

        std::unique_ptr<CBlockTemplate> pblocktemplate;

        size_t i = 0;
        size_t nWaitFor = 60000;
        CAmount reserve_balance;
        {
            {
                LOCK(pwallet->cs_wallet);
                if (nSearchTime <= pwallet->nLastCoinStakeSearchTime) {
                    nWaitFor = std::min(nWaitFor, (size_t)nMinerSleep);
                    continue;
                }

                if (pwallet->IsLocked()) {
                    pwallet->m_is_staking = NOT_STAKING_LOCKED;
                    nWaitFor = std::min(nWaitFor, (size_t)30000);
                    LogPrint(BCLog::POS, "%s: Wallet %d, locked wallet.\n", __func__, i);
                    continue;
                }

                reserve_balance = pwallet->nReserveBalance;
            }

            CAmount balance = pwallet->GetAvailableBalance();

            if (balance <= reserve_balance) {
                LOCK(pwallet->cs_wallet);
                pwallet->m_is_staking = NOT_STAKING_BALANCE;
                nWaitFor = std::min(nWaitFor, (size_t)60000);
                pwallet->nLastCoinStakeSearchTime = nSearchTime + 60;
                LogPrint(BCLog::POS, "%s: Wallet %d, low balance.\n", __func__, i);
                continue;
            }

            if (!pblocktemplate.get()) {
                pblocktemplate = BlockAssembler(Params()).CreateNewBlock(coinbaseScript, true);
                if (!pblocktemplate.get()) {
                    fIsStaking = false;
                    nWaitFor = std::min(nWaitFor, (size_t)nMinerSleep);
                    LogPrint(BCLog::POS, "%s: Couldn't create new block.\n", __func__);
                    continue;
                }
            }

            pwallet->m_is_staking = IS_STAKING;
            nWaitFor = nMinerSleep;
            fIsStaking = true;
            if (wallet.SignBlock(pblocktemplate.get(), nBestHeight + 1, nSearchTime)) {
                CBlock *pblock = &pblocktemplate->block;
                if (CheckStake(pblock)) {
                     nTimeLastStake = GetTime();
                     continue;
                }
            }
            else
            {
                int nRequiredDepth = std::min((int)(COINBASE_MATURITY - 1), (int)(nBestHeight / 2));
                LOCK(pwallet->cs_wallet);
                if (pwallet->m_greatest_txn_depth < nRequiredDepth - 4) {
                    pwallet->m_is_staking = NOT_STAKING_DEPTH;
                    size_t nSleep = (nRequiredDepth - pwallet->m_greatest_txn_depth) / 4;
                    nWaitFor = std::min(nWaitFor, (size_t)(nSleep * 1000));
                    pwallet->nLastCoinStakeSearchTime = nSearchTime + nSleep;
                    LogPrint(BCLog::POS, "%s: Wallet %d, no outputs with required depth, sleeping for %ds.\n", __func__, i, nSleep);
                    continue;
                }
            }
        }

        condWaitFor(nThreadID, nWaitFor);
    }
}

