// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2014 The BlackCoin developers
// Copyright (c) 2017-2021 The Particl Core developers
// Copyright (c) 2022 pacprotocol
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/kernel.h>

#include <chainparams.h>
#include <coins.h>
#include <consensus/validation.h>
#include <hash.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <txmempool.h>
#include <util/system.h>

uint32_t GetStakeTimestampMask(int nHeight)
{
    return nStakeTimestampMask;
}

/**
 * Stake Modifier (hash modifier of proof-of-stake):
 * The purpose of stake modifier is to prevent a txout (coin) owner from
 * computing future proof-of-stake generated by this txout at the time
 * of transaction confirmation. To meet kernel protocol, the txout
 * must hash with a future stake modifier to generate the proof.
 */
uint256 ComputeStakeModifier(const CBlockIndex* pindexPrev, const uint256& kernel)
{
    if (!pindexPrev)
        return uint256(); // genesis block's modifier is 0

    CDataStream ss(SER_GETHASH, 0);
    ss << kernel << pindexPrev->nStakeModifier;
    return Hash(ss.begin(), ss.end());
}

/**
 * BlackCoin kernel protocol
 * coinstake must meet hash target according to the protocol:
 * kernel (input 0) must meet the formula
 *     hash(nStakeModifier + txPrev.block.nTime + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime) < bnTarget * nWeight
 * this ensures that the chance of getting a coinstake is proportional to the
 * amount of coins one owns.
 * The reason this hash is chosen is the following:
 *   nStakeModifier: scrambles computation to make it very difficult to precompute
 *                   future proof-of-stake
 *   txPrev.block.nTime: prevent nodes from guessing a good timestamp to
 *                       generate transaction for future advantage,
 *                       obsolete since v3
 *   txPrev.nTime: slightly scrambles computation
 *   txPrev.vout.hash: hash of txPrev, to reduce the chance of nodes
 *                     generating coinstake at the same time
 *   txPrev.vout.n: output number of txPrev, to reduce the chance of nodes
 *                  generating coinstake at the same time
 *   nTime: current timestamp
 *   block/tx hash should not be used here as they can be generated in vast
 *   quantities so as to generate blocks faster, degrading the system back into
 *   a proof-of-work situation.
 */
bool CheckStakeKernelHash(const CBlockIndex* pindexPrev,
    uint32_t nBits, uint32_t nBlockFromTime,
    CAmount prevOutAmount, const COutPoint& prevout, uint32_t nTime,
    uint256& hashProofOfStake, uint256& targetProofOfStake,
    bool fPrintProofOfStake)
{
    if (nTime < nBlockFromTime) {
        return error("%s: nTime violation", __func__);
    }

    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0) {
        return error("%s: SetCompact failed.", __func__);
    }

    // Weighted target
    int64_t nValueIn = prevOutAmount;
    arith_uint256 bnWeight = arith_uint256(nValueIn);
    bnTarget *= bnWeight;

    targetProofOfStake = ArithToUint256(bnTarget);

    const uint256& nStakeModifier = pindexPrev->nStakeModifier;
    int nStakeModifierHeight = pindexPrev->nHeight;
    int64_t nStakeModifierTime = pindexPrev->nTime;

    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier;
    ss << nBlockFromTime << prevout.hash << prevout.n << nTime;
    hashProofOfStake = Hash(ss.begin(), ss.end());

    if (fPrintProofOfStake) {
        LogPrint(BCLog::POS, "%s: using modifier=%s at height=%d timestamp=%s\n",
            __func__, nStakeModifier.ToString(), nStakeModifierHeight,
            FormatISO8601DateTime(nStakeModifierTime));
        LogPrint(BCLog::POS, "%s: check modifier=%s nTimeKernel=%u nPrevout=%u nTime=%u hashProof=%s\n",
            __func__, nStakeModifier.ToString(),
            nBlockFromTime, prevout.n, nTime,
            hashProofOfStake.ToString());
    }

    // Now check if proof-of-stake hash meets target protocol
    if (UintToArith256(hashProofOfStake) > bnTarget) {
        return false;
    }

    if (LogAcceptCategory(BCLog::POS) && !fPrintProofOfStake) {
        LogPrint(BCLog::POS, "%s: using modifier=%s at height=%d timestamp=%s\n",
            __func__, nStakeModifier.ToString(), nStakeModifierHeight,
            FormatISO8601DateTime(nStakeModifierTime));
        LogPrint(BCLog::POS, "%s: pass modifier=%s nTimeKernel=%u nPrevout=%u nTime=%u hashProof=%s\n",
            __func__, nStakeModifier.ToString(),
            nBlockFromTime, prevout.n, nTime,
            hashProofOfStake.ToString());
    }

    return true;
}

bool GetKernelInfo(const CBlockIndex* blockindex, const CTransaction& tx, uint256& hash, CAmount& value, CScript& script, uint256& blockhash)
{
    if (!blockindex->pprev) {
        return false;
    }

    if (tx.vin.size() < 1) {
        return false;
    }

    const COutPoint& prevout = tx.vin[0].prevout;
    CTransactionRef txPrev;
    uint256 blockKernelHash; // hash of block containing stake kernel
    if (!GetTransaction(prevout.hash, txPrev, Params().GetConsensus(), blockKernelHash) || prevout.n >= txPrev->vout.size()) {
        return false;
    }

    const CTxOut outPrev = txPrev->vout[prevout.n];
    value = outPrev.nValue;
    script = outPrev.scriptPubKey;
    blockhash = blockKernelHash;
    CBlockIndex* stakeIndex = LookupBlockIndex(blockhash);
    if (!stakeIndex) {
        return false;
    }
    uint32_t nBlockFromTime = stakeIndex->nTime;
    uint32_t nTime = blockindex->nTime;

    CDataStream ss(SER_GETHASH, 0);
    ss << blockindex->pprev->nStakeModifier;
    ss << nBlockFromTime << prevout.hash << prevout.n << nTime;
    hash = Hash(ss.begin(), ss.end());

    return true;
};

bool CheckProofOfStake(CValidationState& state, const CBlockIndex* pindexPrev, const CTransaction& tx, int64_t nTime, unsigned int nBits, uint256& hashProofOfStake, uint256& targetProofOfStake)
{
    // pindexPrev is the current tip, the block the new block will connect on to
    // nTime is the time of the new/next block

    if (!tx.IsCoinStake() || tx.vin.size() < 1) {
        return false;
    }

    CTransactionRef txPrev;

    // Kernel (input 0) must match the stake hash target per coin age (nBits)
    const CTxIn& txin = tx.vin[0];

    uint32_t nBlockFromTime;
    int nDepth;
    CScript kernelPubKey;
    CAmount amount;

    Coin coin;
    if (!::ChainstateActive().CoinsTip().GetCoin(txin.prevout, coin) || coin.IsSpent()) {
        return false;
    }

    CBlockIndex* pindex = ::ChainActive()[coin.nHeight];
    if (!pindex) {
        return false;
    }

    nDepth = pindexPrev->nHeight - coin.nHeight;
    int nRequiredDepth = std::min((int)(COINBASE_MATURITY - 1), (int)(pindexPrev->nHeight / 2));
    if (nRequiredDepth > nDepth) {
        LogPrint(BCLog::POS, "ERROR: %s: Tried to stake at depth %d\n", __func__, nDepth + 1);
        return false;
    }

    kernelPubKey = coin.out.scriptPubKey;
    amount = coin.out.nValue;
    nBlockFromTime = pindex->GetBlockTime();
    const CScript& scriptSig = txin.scriptSig;
    ScriptError serror = SCRIPT_ERR_OK;

    // Redundant: all inputs are checked later during CheckInputs
    if (!VerifyScript(scriptSig, kernelPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&tx, 0, amount), &serror)) {
        LogPrint(BCLog::POS, "ERROR: %s: verify-script-failed, txn %s, reason %s\n", __func__, tx.GetHash().ToString(), ScriptErrorString(serror));
        return state.DoS(100,  "verify-cs-script-failed");
    }

    if (!CheckStakeKernelHash(pindexPrev, nBits, nBlockFromTime, amount, txin.prevout, nTime, hashProofOfStake, targetProofOfStake, LogAcceptCategory(BCLog::POS))) {
        LogPrint(BCLog::POS, "WARNING: %s: Check kernel failed on coinstake %s, hashProof=%s\n", __func__, tx.GetHash().ToString(), hashProofOfStake.ToString());
        return state.DoS(100, "check-kernel-failed");
    }

    return true;
}

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int nHeight, int64_t nTimeBlock)
{
    return (nTimeBlock & GetStakeTimestampMask(nHeight)) == 0;
}

// Used only when staking, not during validation
bool CheckKernel(const CBlockIndex* pindexPrev, unsigned int nBits, int64_t nTime, const COutPoint& prevout, int64_t* pBlockTime)
{
    uint256 hashProofOfStake, targetProofOfStake;

    Coin coin;
    {
        LOCK(::cs_main);
        if (!::ChainstateActive().CoinsTip().GetCoin(prevout, coin)) {
            return error("%s: prevout not found", __func__);
        }
    }

    if (coin.IsSpent()) {
        return error("%s: prevout is spent", __func__);
    }

    CBlockIndex* pindex = ::ChainActive()[coin.nHeight];
    if (!pindex) {
        return false;
    }

    int nRequiredDepth = std::min((int)(COINBASE_MATURITY - 1), (int)(pindexPrev->nHeight / 2));
    int nDepth = pindexPrev->nHeight - coin.nHeight;

    if (nRequiredDepth > nDepth) {
        return false;
    }

    if (pBlockTime) {
        *pBlockTime = pindex->GetBlockTime();
    }

    CAmount amount = coin.out.nValue;
    return CheckStakeKernelHash(pindexPrev, nBits, *pBlockTime, amount, prevout, nTime, hashProofOfStake, targetProofOfStake);
}
