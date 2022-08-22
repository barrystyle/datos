// Copyright (c) 2022 pacprotocol
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/stakegen.h>

#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <index/disktxpos.h>
#include <index/txindex.h>
#include <pos/kernel.h>
#include <pos/minter.h>
#include <pos/signature.h>
#include <pos/stakeseen.h>
#include <pow.h>
#include <wallet/coincontrol.h>

void CStakeWallet::AvailableCoinsForStaking(std::vector<COutput> &vCoins, int64_t nTime, int nHeight) const
{
    vCoins.clear();
    wallet->m_greatest_txn_depth = 0;

    {
        LOCK(wallet->cs_wallet);

        int nowTime = GetTime();
        int nHeight = ::ChainActive().Height();
        int min_stake_confirmations = COINBASE_MATURITY;
        int nRequiredDepth = std::min(min_stake_confirmations-1, (int)(nHeight / 2));

        for (const auto &walletEntry : wallet->mapWallet) {
            const CWalletTx* wtx = &walletEntry.second;
            CTransactionRef tx = wtx->tx;

            auto locked_chain = wallet->chain().lock();
            int nDepth = wtx->GetDepthInMainChain(*locked_chain);
            if (nDepth > wallet->m_greatest_txn_depth) {
                wallet->m_greatest_txn_depth = nDepth;
            }

            if (nDepth < nRequiredDepth) {
                continue;
            }

            if (wtx->IsCoinStake() && min_stake_confirmations < COINBASE_MATURITY) {
                // min_stake_confirmations is only less than COINBASE_MATURITY in regtest mode
                if (nDepth < std::min(COINBASE_MATURITY, (int)(nHeight / 2))) {
                    continue;
                }
            }

            const uint256 &wtxid = walletEntry.first;
            for (size_t i = 0; i < tx->vout.size(); ++i)
            {
                const auto &txout = tx->vout[i];
                if (txout.nValue < params.nStakeMinValue || txout.nValue > params.nStakeMaxValue) {
                    continue;
                }

                int input_age = nowTime - wtx->GetTxTime();
                if (input_age < params.nStakeMinAge || input_age > params.nStakeMaxAge) {
                    continue;
                }

                CKeyID keyID;
                COutPoint kernel(wtxid, i);
                const CScript pscriptPubKey = txout.scriptPubKey;
                if (!ExtractStakingKeyID(pscriptPubKey, keyID)) {
                    continue;
                }

                if (!CheckStakeUnused(kernel) || wallet->IsSpent(*locked_chain, wtxid, i) || wallet->IsLockedCoin(wtxid, i)) {
                    continue;
                }

                isminetype mine = wallet->IsMine(txout);
                if (!(mine & ISMINE_SPENDABLE)) {
                    continue;
                }

                bool fSpendableIn = true;
                bool fSolvableIn = true;

                int input_bytes = 0; // unnecessary
                vCoins.emplace_back(wtx, i, nDepth, input_bytes, fSpendableIn, fSolvableIn, true);
            }
        }
    }

    Shuffle(vCoins.begin(), vCoins.end(), FastRandomContext());
    return;
}

bool CStakeWallet::SelectCoinsForStaking(CAmount nTargetValue, int64_t nTime, int nHeight, std::set<std::pair<const CWalletTx*, unsigned int>>& setCoinsRet, CAmount& nValueRet) const
{
    if (!ready) {
        return false;
    }

    std::vector<COutput> vCoins;
    AvailableCoinsForStaking(vCoins, nTime, nHeight);

    setCoinsRet.clear();
    nValueRet = 0;

    for (const auto& output : vCoins) {
        const CWalletTx* pcoin = output.tx;
        int i = output.i;

        // Stop if we've chosen enough inputs
        if (nValueRet >= nTargetValue) {
            break;
        }

        CAmount n = pcoin->tx->vout[i].nValue;

        std::pair<int64_t, std::pair<const CWalletTx*, unsigned int>> coin = std::make_pair(n, std::make_pair(pcoin, i));

        if (n >= nTargetValue) {
            // If input value is greater or equal to target then simply insert
            //    it into the current subset and exit
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            break;
        } else {
            if (n < nTargetValue + CENT) {
                setCoinsRet.insert(coin.second);
                nValueRet += coin.first;
            }
        }
    }

    return true;
}

uint64_t CStakeWallet::GetStakeWeight(interfaces::Chain::Lock& locked_chain) const
{
    if (!ready) {
        return 0;
    }

    // Choose coins to use
    CAmount nBalance = wallet->GetBalance().m_mine_trusted;

    if (nBalance <= wallet->nReserveBalance) {
        return 0;
    }

    CAmount nValueIn = 0;
    std::vector<const CWalletTx*> vwtxPrev;
    std::set<std::pair<const CWalletTx*,unsigned int> > setCoins;

    CAmount nTargetValue = nBalance - wallet->nReserveBalance;
    if (!SelectCoinsForStaking(nTargetValue, 0, 0, setCoins, nValueIn)) {
        return 0;
    }

    if (setCoins.empty()) {
        return 0;
    }

    uint64_t nWeight = 0;
    for(std::pair<const CWalletTx*,unsigned int> pcoin : setCoins) {
        if (pcoin.first->GetDepthInMainChain(locked_chain) >= COINBASE_MATURITY) {
            nWeight += pcoin.first->tx->vout[pcoin.second].nValue;
        }
    }

    return nWeight;
}

bool CStakeWallet::CreateCoinStake(CBlockIndex* pindexPrev, unsigned int nBits, int64_t nTime, int nBlockHeight, int64_t nFees, CMutableTransaction& txNew, CKey& key)
{
    if (!ready) {
        return false;
    }

    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);
    CAmount nBalance = wallet->GetAvailableBalance();
    if (nBalance <= wallet->nReserveBalance) {
        return false;
    }

    // Ensure txn is empty
    txNew.vin.clear();
    txNew.vout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vout.push_back(CTxOut(0, scriptEmpty));

    // Choose coins to use
    CAmount nValueIn = 0;
    std::vector<const CWalletTx*> vwtxPrev;
    std::set<std::pair<const CWalletTx*, unsigned int>> setCoins;
    if (!SelectCoinsForStaking(nBalance - wallet->nReserveBalance, nTime, nBlockHeight, setCoins, nValueIn)) {
        UninterruptibleSleep(std::chrono::milliseconds{150});
        return false;
    }

    if (setCoins.empty()) {
        UninterruptibleSleep(std::chrono::milliseconds{150});
        return false;
    }

    CAmount nCredit = 0;
    CScript scriptPubKeyKernel;
    std::set<std::pair<const CWalletTx*, unsigned int>>::iterator it = setCoins.begin();

    for (; it != setCoins.end(); ++it)
    {
        auto pcoin = *it;
        if (ThreadStakeMinerStopped()) {
            return false;
        }

        int64_t nBlockTime;
        COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
        if (CheckKernel(pindexPrev, nBits, nTime, prevoutStake, &nBlockTime))
        {
            LOCK(wallet->cs_wallet);

            // Found a kernel
            LogPrint(BCLog::POS, "%s: Kernel found.\n", __func__);

            CTxOut kernelOut = pcoin.first->tx->vout[pcoin.second];

            CScript scriptPubKeyOut;
            std::vector<valtype> vSolutions;
            CScript scriptPubKeyKernel = pcoin.first->tx->vout[pcoin.second].scriptPubKey;
            txnouttype whichType = Solver(scriptPubKeyKernel, vSolutions);

            LogPrint(BCLog::POS, "%s: parsed kernel type=%s\n", __func__, GetTxnOutputType(whichType));

            if (whichType == TX_PUBKEYHASH) {

                uint160 hash160(vSolutions[0]);
                CKeyID pubKeyHash(hash160);
                if (!wallet->GetKey(pubKeyHash, key)) {
                    LogPrint(BCLog::POS, "%s: failed to get key for kernel type=%s\n", __func__, GetTxnOutputType(whichType));
                    break;
                }
                scriptPubKeyOut << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;

            } else if (whichType == TX_PUBKEY) {

                valtype& vchPubKey = vSolutions[0];
                CPubKey pubKey(vchPubKey);
                uint160 hash160(Hash160(vchPubKey));
                CKeyID pubKeyHash(hash160);
                if (!wallet->GetKey(pubKeyHash, key)) {
                    LogPrint(BCLog::POS, "%s: failed to get key for kernel type=%s\n", __func__, GetTxnOutputType(whichType));
                    break;
                }
                if (key.GetPubKey() != pubKey) {
                    LogPrint(BCLog::POS, "%s: invalid key for kernel type=%s\n", __func__, GetTxnOutputType(whichType));
                    break; // keys mismatch
                }
                scriptPubKeyOut = scriptPubKeyKernel;

            } else {
                LogPrint(BCLog::POS, "%s: no support for kernel type=%s\n", __func__, GetTxnOutputType(whichType));
                continue;
            }

            txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
            nCredit += pcoin.first->tx->vout[pcoin.second].nValue;
            vwtxPrev.push_back(pcoin.first);
            CTxOut out(0, scriptPubKeyOut);
            txNew.vout.push_back(out);

            LogPrint(BCLog::POS, "%s: Added kernel.\n", __func__);

            setCoins.erase(it);
            break;
        }
    }

    if (nCredit == 0 || nCredit > nBalance - wallet->nReserveBalance) {
        return false;
    }

    // Attempt to add more inputs
    // Only advantage here is to setup the next stake using this output as a kernel to have a higher chance of staking
    size_t nStakesCombined = 0;
    it = setCoins.begin();
    while (it != setCoins.end())
    {
        if (nStakesCombined >= wallet->nMaxStakeCombine) {
            break;
        }

        // Stop adding more inputs if already too many inputs
        if (txNew.vin.size() >= 100) {
            break;
        }

        // Stop adding more inputs if value is already pretty significant
        if (nCredit >= wallet->nStakeCombineThreshold) {
            break;
        }

        std::set<std::pair<const CWalletTx*, unsigned int>>::iterator itc = it++; // copy the current iterator then increment it
        auto pcoin = *itc;
        CTxOut prevOut = pcoin.first->tx->vout[pcoin.second];

        // Only add coins of the same key/address as kernel
        if (prevOut.scriptPubKey != scriptPubKeyKernel) {
            continue;
        }

        // Stop adding inputs if reached reserve limit
        if (nCredit + prevOut.nValue > nBalance - wallet->nReserveBalance) {
            break;
        }

        // Do not add additional significant input
        if (prevOut.nValue >= wallet->nStakeCombineThreshold) {
            continue;
        }

        txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
        nCredit += pcoin.first->tx->vout[pcoin.second].nValue;
        vwtxPrev.push_back(pcoin.first);

        LogPrint(BCLog::POS, "%s: Combining kernel %s, %d.\n", __func__, pcoin.first->GetHash().ToString(), pcoin.second);
        nStakesCombined++;
        setCoins.erase(itc);
    }

    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Get block reward
    CAmount nReward = GetProofOfStakeReward(pindexPrev, nFees);
    if (nReward < 0) {
        return false;
    }

    nCredit += nReward;
    {
        if (nCredit >= wallet->nStakeSplitThreshold) {
            txNew.vout.push_back(CTxOut(0, txNew.vout[1].scriptPubKey));
        }

        // Set output amount
        if (txNew.vout.size() == 3)
        {
            txNew.vout[1].nValue = (nCredit / 2 / CENT) * CENT;
            txNew.vout[2].nValue = nCredit - txNew.vout[1].nValue;
        }
        else
        {
            txNew.vout[1].nValue = nCredit;
        }
    }

    // Sign
    int nIn = 0;
    for (const auto& pcoin : vwtxPrev)
    {
        uint32_t nPrev = txNew.vin[nIn].prevout.n;
        CTxOut prevOut = pcoin->tx->vout[nPrev];
        CAmount amount = prevOut.nValue;
        CScript& scriptPubKeyOut = prevOut.scriptPubKey;

        SignatureData sigdata;
        if (!ProduceSignature(*wallet, MutableTransactionSignatureCreator(&txNew, nIn, amount, SIGHASH_ALL), scriptPubKeyOut, sigdata)) {
            return error("%s: ProduceSignature failed.", __func__);
        }

        UpdateInput(txNew.vin[nIn], sigdata);
        nIn++;
    }

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
    if (nBytes >= MaxBlockSize() / 5) {
        return error("%s: Exceeded coinstake size limit.", __func__);
    }

    // Successfully generated coinstake
    return true;
}

void CStakeWallet::AbandonOrphanedCoinstakes() const
{
    if (!ready) {
        return;
    }

    auto locked_chain = wallet->chain().lock();
    for (std::pair<const uint256, CWalletTx>& item : wallet->mapWallet) {
        const uint256& wtxid = item.first;
        CWalletTx& wtx = item.second;
        assert(wtx.GetHash() == wtxid);
        if (wtx.GetDepthInMainChain(*locked_chain) == 0 && !wtx.isAbandoned() && wtx.IsCoinStake()) {
            LogPrint(BCLog::POS, "Abandoning coinstake wtx %s\n", wtx.GetHash().ToString());
            if (!wallet->AbandonTransaction(*locked_chain, wtxid)) {
                LogPrint(BCLog::POS, "Failed to abandon coinstake tx %s\n", wtx.GetHash().ToString());
            }
        }
    }
}

bool CStakeWallet::SignBlock(CBlockTemplate* pblocktemplate, int nHeight, int64_t nSearchTime)
{
    if (!ready) {
        return false;
    }

    LogPrint(BCLog::POS, "%s, Height %d\n", __func__, nHeight);

    AbandonOrphanedCoinstakes();

    assert(pblocktemplate);
    CBlock* pblock = &pblocktemplate->block;
    assert(pblock);
    if (pblock->vtx.size() < 1) {
        return error("%s: Malformed block.", __func__);
    }

    CAmount nFees = -pblocktemplate->vTxFees[0];
    CBlockIndex* pindexPrev = ::ChainActive().Tip();

    CKey key;
    pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, Params().GetConsensus());
    LogPrint(BCLog::POS, "%s, nBits %d\n", __func__, pblock->nBits);

    CMutableTransaction txCoinStake;
    if (CreateCoinStake(pindexPrev, pblock->nBits, nSearchTime, nHeight, nFees, txCoinStake, key)) {
        LogPrint(BCLog::POS, "%s: Kernel found.\n", __func__);

        if (nSearchTime >= ::ChainActive().Tip()->GetPastTimeLimit() + 1) {

            // make sure coinstake would meet timestamp protocol
            //    as it would be the same as the block timestamp
            pblock->nTime = nSearchTime;

            // Insert coinstake as vtx[1]
            pblock->vtx.insert(pblock->vtx.begin() + 1, MakeTransactionRef(txCoinStake));

            bool mutated;
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock, &mutated);

            uint256 blockhash = pblock->GetHash();
            LogPrint(BCLog::POS, "%s: signing blockhash %s\n", __func__, blockhash.ToString());

            // Append a signature to the block
            return SignBlockWithKey(*pblock, key);
        }
    }

    wallet->nLastCoinStakeSearchTime = nSearchTime;

    return false;
}

CWallet* CStakeWallet::GetStakingWallet()
{
    if (!ready) {
        return nullptr;
    }

    return wallet;
}
