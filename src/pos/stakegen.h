// Copyright (c) 2022 pacprotocol
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef POS_STAKEGEN_H
#define POS_STAKEGEN_H

#include <chainparams.h>
#include <consensus/tx_verify.h>
#include <index/disktxpos.h>
#include <index/txindex.h>
#include <miner.h>
#include <pos/kernel.h>
#include <index/txindex.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

using valtype = std::vector<unsigned char>;

static const CAmount CENT = 1000000;

/**
 * Convenience class allowing stake functions to have easy access to the wallet,
 * without the linking issues that come with later bitcoin releases.
 */
class CStakeWallet
{
    private:
        bool ready;
        CWallet* wallet;
        Consensus::Params params;

    public:
        CStakeWallet()
        {
            ready = false;
            wallet = nullptr;
        }

        bool IsReady() { return ready; }
        void SetReady() { ready = true; }
        void UnsetReady() { ready = false; }

        void SetParams() { params = Params().GetConsensus(); }

        void AttachWallet(CWallet* pwallet) {
            if (!pwallet) return;
            wallet = pwallet;
            SetReady();
        }

        void RemoveWallet() {
            wallet = nullptr;
            UnsetReady();
        }

        void AvailableCoinsForStaking(std::vector<COutput> &vCoins) const;
        bool SelectCoinsForStaking(CAmount nTargetValue, std::set<std::pair<const CWalletTx*, unsigned int>>& setCoinsRet, CAmount& nValueRet) const;
        uint64_t GetStakeWeight(interfaces::Chain::Lock& locked_chain) const;
        bool CreateCoinStake(CBlockIndex* pindexPrev, unsigned int nBits, int64_t nTime, int nBlockHeight, int64_t nFees, CMutableTransaction& txNew, CKey& key);
        void AbandonOrphanedCoinstakes() const;
        bool SignBlock(CBlockTemplate* pblocktemplate, int nHeight, int64_t nSearchTime);
        CWallet* GetStakingWallet();
};

#endif // POS_STAKEGEN_H
