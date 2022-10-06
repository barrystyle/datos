// Copyright (c) 2021 pacprotocol
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/util.h>

extern CTxMemPool mempool;

void TokenSafetyChecks()
{
    if (!AreTokensActive()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot perform token action while token layer is not active");
    }

    if (::ChainstateActive().IsInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot perform token action while still in Initial Block Download");
    }
}

bool AreTokensActive(int height)
{
    const Consensus::Params& params = Params().GetConsensus();
    //! check against provided height
    if (height != 0) {
        return height >= params.nTokenHeight;
    }
    //! otherwise use active chainheight
    return ::ChainActive().Height() >= params.nTokenHeight;
}

void ReclaimInvalidInputs()
{
    auto vpwallets = GetWallets();
    size_t nWallets = vpwallets.size();

    if (nWallets < 1) {
        return;
    }

    for (size_t i = 0; i < nWallets; ++i) {
        vpwallets[i].get()->AbandonInvalidTransaction();
    }
}

bool CompareTokenName(std::string& PrevTokenName, std::string& TokenName)
{
    return (PrevTokenName.compare(TokenName) == 0);
}

bool CheckTokenName(std::string& tokenName, std::string& errorReason)
{
    if (tokenName.length() < TOKENNAME_MINLEN || tokenName.length() > TOKENNAME_MAXLEN) {
        errorReason = "tokenname-bounds-exceeded";
        return false;
    }

    std::string cleanedName = SanitizeString(tokenName);
    if (cleanedName.length() != tokenName.length()) {
        errorReason = "tokenname-bounds-inconsistent";
        return false;
    }

    std::string casedName = boost::algorithm::to_upper_copy(boost::algorithm::to_lower_copy(tokenName));
    if (tokenName != casedName) {
        errorReason = "tokenname-not-upper";
        return false;
    }

    if (cleanedName.compare(tokenName) != 0) {
        errorReason = "tokenname-payload-inconsistent";
        return false;
    }

    return true;
}

void StripControlChars(std::string& instr)
{
    std::string outstr;
    outstr.clear();
    for (int i = 0; i < instr.size(); i++) {
        if (std::isalnum(instr[i])) {
            outstr += instr[i];
        }
    }
    instr = outstr;
}

bool IsInMempool(uint256& txhash)
{
    LOCK(mempool.cs);
    if (mempool.exists(txhash)) {
        return true;
    }
    return false;
}

void RemoveFromMempool(CTransaction& tx)
{
    LOCK(mempool.cs);
    mempool.removeRecursive(tx, MemPoolRemovalReason::CONFLICT);
}

bool IsOutputUnspent(const COutPoint& out)
{
    Coin coin;
    if (!GetUTXOCoin(out, coin)) {
        return false;
    }
    return true;
}

bool IsOutputInMempool(const COutPoint& out)
{
    LOCK(mempool.cs);

    //! build quick vin cache
    std::vector<COutPoint> MempoolOutputs;
    for (const auto& l : mempool.mapTx) {
        const CTransaction& mtx = l.GetTx();
        if (mtx.HasTokenOutput()) {
            for (unsigned int i = 0; i < mtx.vin.size(); i++) {
                MempoolOutputs.push_back(mtx.vin[i].prevout);
            }
        }
    }

    //! then see if our output is in this cache
    const auto& it = std::find(MempoolOutputs.begin(), MempoolOutputs.end(), out);
    if (it != MempoolOutputs.end()) {
        return true;
    }

    return false;
}

int TokentxInMempool()
{
    LOCK(mempool.cs);

    int TokenTotal = 0;
    for (const auto& l : mempool.mapTx) {
        const CTransaction& mtx = l.GetTx();
        bool is_token_tx = true;
        for (const auto& m : mtx.vout) {
            if (m.IsStandardOutput()) {
                is_token_tx = false;
            }
        }
        if (is_token_tx) {
            TokenTotal++;
        }
    }

    return TokenTotal;
}

void PrintTxinFunds(std::vector<CTxIn>& FundsRet)
{
    unsigned int n = 0;
    for (CTxIn input : FundsRet) {
        LogPrint(BCLog::TOKEN, "%s - input %d - %s\n", __func__, n++, input.ToString());
    }
}

opcodetype GetOpcode(int n)
{
    opcodetype ret = OP_0;
    switch (n) {
    case 1:
        ret = OP_1;
        break;
    case 2:
        ret = OP_2;
        break;
    case 3:
        ret = OP_3;
        break;
    case 4:
        ret = OP_4;
        break;
    case 5:
        ret = OP_5;
        break;
    case 6:
        ret = OP_6;
        break;
    case 7:
        ret = OP_7;
        break;
    case 8:
        ret = OP_8;
        break;
    case 9:
        ret = OP_9;
        break;
    case 10:
        ret = OP_10;
        break;
    case 11:
        ret = OP_11;
        break;
    case 12:
        ret = OP_12;
        break;
    case 13:
        ret = OP_13;
        break;
    case 14:
        ret = OP_14;
        break;
    case 15:
        ret = OP_15;
        break;
    case 16:
        ret = OP_16;
        break;
    default:
        break;
    }
    return ret;
}

int GetIntFromOpcode(opcodetype n)
{
    int ret = 0;

    switch (n) {
    case OP_1:
        ret = 1;
        break;
    case OP_2:
        ret = 2;
        break;
    case OP_3:
        ret = 3;
        break;
    case OP_4:
        ret = 4;
        break;
    case OP_5:
        ret = 5;
        break;
    case OP_6:
        ret = 6;
        break;
    case OP_7:
        ret = 7;
        break;
    case OP_8:
        ret = 8;
        break;
    case OP_9:
        ret = 9;
        break;
    case OP_10:
        ret = 10;
        break;
    case OP_11:
        ret = 11;
        break;
    case OP_12:
        ret = 12;
        break;
    case OP_13:
        ret = 13;
        break;
    case OP_14:
        ret = 14;
        break;
    case OP_15:
        ret = 15;
        break;
    case OP_16:
        ret = 16;
        break;
    default:
        break;
    }
    return ret;
}
