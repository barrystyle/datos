// Copyright (c) 2021 pacprotocol
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/verify.h>

bool CheckTokenMempool(CTxMemPool& pool, const CTransactionRef& tx, std::string& strError)
{
    LOCK(mempool.cs);

    // we are checking and ensuring that all token inputs have minimum confirms,
    // and also if any duplicate issuance token names exist (before they get committed to KnownIssuances via connectblock)

    //! check inputs have sufficient confirms
    CCoinsViewCache& view = ::ChainstateActive().CoinsTip();
    CBlockIndex* pindex = ::ChainActive().Tip();
    if (!CheckTokenInputs(tx, pindex, view, strError)) {
        return false;
    }

    //! build issuance name list from mempool
    std::vector<std::string> MempoolNames;
    for (const auto& l : pool.mapTx) {
        const CTransaction& mtx = l.GetTx();
        if (mtx.HasTokenOutput()) {
            for (unsigned int i = 0; i < mtx.vout.size(); i++) {
                if (mtx.vout[i].scriptPubKey.IsPayToToken()) {
                    CToken token;
                    CScript TokenScript = mtx.vout[i].scriptPubKey;
                    if (!ContextualCheckToken(TokenScript, token, strError)) {
                        LogPrint(BCLog::TOKEN, "ContextualCheckToken returned with error '%s'\n", strError);
                        strError = "corrupt-invalid-existing-mempool";
                        return false;
                    }
                    std::string name = token.getName();
                    if (token.getType() == CToken::ISSUANCE) {
                        if (std::find(MempoolNames.begin(), MempoolNames.end(), name) == MempoolNames.end()) {
                            MempoolNames.push_back(name);
                        }
                    }
                }
            }
        }
    }

    //! check if our new issuance already exists in this pool
    for (unsigned int i = 0; i < tx->vout.size(); i++) {
        CToken token;
        if (tx->vout[i].scriptPubKey.IsPayToToken()) {
            CScript TokenScript = tx->vout[i].scriptPubKey;
            if (!ContextualCheckToken(TokenScript, token, strError)) {
                LogPrint(BCLog::TOKEN, "ContextualCheckToken returned with error '%s'\n", strError);
                strError = "corrupt-invalid-tokentx-mempool";
                return false;
            }
            std::string name = token.getName();
            if (token.getType() == CToken::ISSUANCE) {
                if (std::find(MempoolNames.begin(), MempoolNames.end(), name) != MempoolNames.end()) {
                    strError = "token-issuance-exists-mempool";
                    return false;
                }
            }
        }
    }

    return true;
}

bool IsIdentifierInRange(uint64_t& identifier)
{
    bool inRange = (identifier > ISSUANCE_ID_BEGIN) && (identifier < (std::numeric_limits<uint64_t>::max() - ISSUANCE_ID_BEGIN));
    LogPrint(BCLog::TOKEN, "%s: returning %s\n", __func__, inRange);
    return inRange;
}

bool CheckTokenIssuance(const CTransactionRef& tx, std::string& strError, bool onlyCheck)
{
    uint256 hash = tx->GetHash();
    for (unsigned int i = 0; i < tx->vout.size(); i++) {
        if (tx->vout[i].scriptPubKey.IsPayToToken()) {
            CToken token;
            CScript TokenScript = tx->vout[i].scriptPubKey;
            if (!ContextualCheckToken(TokenScript, token, strError)) {
                LogPrint(BCLog::TOKEN, "ContextualCheckToken returned with error %s\n", strError);
                return false;
            }
            token.setOriginTx(hash);
            if (token.getType() == CToken::ISSUANCE) {
                //! we're only reading from it, so this is fine
                std::vector<CToken> TempIssuances = CopyIssuancesVector();
                for (CToken& issued : TempIssuances) {
                    if (issued.getOriginTx() != token.getOriginTx()) {
                        if (issued.getName() == token.getName()) {
                            strError = "issuance-name-exists";
                            return false;
                        } else if (issued.getId() == token.getId()) {
                            strError = "issuance-id-exists";
                            return false;
                        }
                    }
                }
                std::string name = token.getName();
                uint64_t identifier = token.getId();
                if (!onlyCheck && !IsIdentifierInRange(identifier)) {
                    strError = "token-identifier-out-of-range";
                    return false;
                }
                if (!onlyCheck && (!IsNameInIssuances(name) && !IsIdentifierInIssuances(identifier))) {
                    AddToIssuances(token);
                    LogPrint(BCLog::TOKEN, "%s: added token to issuances %s", __func__, token.ToString());
                }
            } else if (token.getType() == CToken::NONE) {
                return false;
            }
        }
    }
    return true;
}

bool ContextualCheckToken(CScript& TokenScript, CToken& token, std::string& strError, bool debug)
{
    if (!BuildTokenFromScript(TokenScript, token, debug)) {
        LogPrint(BCLog::TOKEN, "BuildTokenFromScript failed\n");
    }

    if (token.getVersion() != CToken::CURRENT_VERSION) {
        strError = "bad-token-version";
        return false;
    }

    if (token.getType() == CToken::NONE) {
        strError = "bad-token-uninit";
        return false;
    }

    if (token.getType() != CToken::ISSUANCE && token.getType() != CToken::TRANSFER) {
        strError = "bad-token-type";
        return false;
    }

    std::string name = token.getName();
    if (!CheckTokenName(name, strError)) {
        return false;
    }

    return true;
}

bool CheckTokenInputs(const CTransactionRef& tx, const CBlockIndex* pindex, const CCoinsViewCache& view, std::string& strError)
{
    if (!tx->HasTokenOutput()) {
        return true;
    }

    int spentHeight = pindex->nHeight;
    for (unsigned int i = 0; i < tx->vin.size(); i++) {
        const COutPoint& prevout = tx->vin[i].prevout;
        const Coin& coin = view.AccessCoin(prevout);
        int confirmations = spentHeight - coin.nHeight;

        LogPrint(BCLog::TOKEN, "%s: COutPoint (%s, %d) has %d confirms, want %d confirm\n",
            __func__, prevout.hash.ToString(), prevout.n, confirmations, TOKEN_MINCONFS);

        if (confirmations < TOKEN_MINCONFS) {
            strError = "token-vin-insufficient-confirms";
            return false;
        }
    }

    return true;
}

bool CheckToken(const CTransactionRef& tx, const CBlockIndex* pindex, const CCoinsViewCache& view, std::string& strError, const Consensus::Params& params, bool onlyCheck)
{
    uint256 hash = tx->GetHash();

    //! check inputs have sufficient confirms
    if (!CheckTokenInputs(tx, pindex, view, strError)) {
        LogPrint(BCLog::TOKEN, "CheckTokenInputs returned with error %s\n", strError);
        return false;
    }

    //! ensure only one issuance per tx
    int issuance_total = 0;
    for (unsigned int i = 0; i < tx->vout.size(); i++) {
        if (tx->vout[i].scriptPubKey.IsPayToToken()) {
            CToken token;
            CScript tokenData = tx->vout[i].scriptPubKey;
            if (!ContextualCheckToken(tokenData, token, strError)) {
                LogPrint(BCLog::TOKEN, "ContextualCheckToken returned with error %s\n", strError);
                strError = "token-isinvalid";
                return false;
            }
            if (token.isIssuance()) {
                ++issuance_total;
            }
        }
    }
    if (issuance_total > 1) {
        strError = "multiple-token-issuances";
        return false;
    }

    //! check to see if token has valid prevout
    for (unsigned int i = 0; i < tx->vout.size(); i++) {

        //! find the token outputs
        if (tx->vout[i].scriptPubKey.IsPayToToken()) {

            //! extract token data from output
            CToken token;
            CScript tokenData = tx->vout[i].scriptPubKey;
            if (!ContextualCheckToken(tokenData, token, strError)) {
                LogPrint(BCLog::TOKEN, "ContextualCheckToken returned with error %s\n", strError);
                strError = "token-isinvalid";
                return false;
            }

            //! check if issuance token is unique
            if (token.getType() == CToken::ISSUANCE) {
                if (!CheckTokenIssuance(tx, strError, onlyCheck)) {
                    LogPrint(BCLog::TOKEN, "CheckTokenIssuance returned with error %s\n", strError);
                    //! if this made its way into mempool, remove it
                    if (IsInMempool(hash)) {
                        CTransaction toBeRemoved(*tx);
                        RemoveFromMempool(toBeRemoved);
                    }
                    return false;
                }
            }

            //! keep identifier and name
            uint64_t tokenId = token.getId();
            std::string tokenName = token.getName();

            //! check token inputs
            for (unsigned int n = 0; n < tx->vin.size(); n++) {
                //! retrieve prevtx
                uint256 prevBlockHash;
                CTransactionRef inputPrev;
                if (!GetTransaction(tx->vin[n].prevout.hash, inputPrev, params, prevBlockHash)) {
                    strError = "token-prevtx-invalid";
                    return false;
                }

                // check if issuances inputs are token related
                uint16_t tokenType = token.getType();
                bool isPrevToken = inputPrev->vout[tx->vin[n].prevout.n].scriptPubKey.IsPayToToken();
                switch (tokenType) {
                case CToken::ISSUANCE:
                    if (isPrevToken) {
                        strError = "token-issuance-prevout-not-standard";
                        return false;
                    }
                    continue;
                case CToken::TRANSFER:
                    if (!isPrevToken) {
                        strError = "token-transfer-prevout-is-invalid";
                        return false;
                    }
                    break;
                case CToken::NONE:
                    strError = "token-type-unusable";
                    return false;
                }

                //! extract prevtoken data from the output
                CToken prevToken;
                CScript prevTokenData = inputPrev->vout[tx->vin[n].prevout.n].scriptPubKey;
                if (!ContextualCheckToken(prevTokenData, prevToken, strError, false)) {
                    LogPrint(BCLog::TOKEN, "ContextualCheckToken returned with error %s\n", strError);
                    strError = "token-prevtoken-isinvalid";
                    return false;
                }

                //! check if token name same as prevtoken name
                uint64_t prevIdentifier = prevToken.getId();
                std::string prevTokenName = prevToken.getName();
                if (!CompareTokenName(prevTokenName, tokenName)) {
                    strError = "prevtoken-isunknown-name";
                    return false;
                }

                if (prevIdentifier != tokenId) {
                    strError = "prevtoken-isunknown-id";
                    return false;
                }
            }
        }
    }

    return true;
}

bool FindLastTokenUse(std::string& name, COutPoint& TokenSpend, int lastHeight, const Consensus::Params& params)
{
    for (int height = lastHeight; height > params.nTokenHeight; --height) {

        // fetch index for current height
        const CBlockIndex* pindex = ::ChainActive()[height];

        // read block from disk
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, params)) {
            continue;
        }

        for (unsigned int i = 0; i < block.vtx.size(); i++) {

            // search for token transactions
            const CTransactionRef& tx = block.vtx[i];
            if (!tx->HasTokenOutput()) {
                continue;
            }

            for (unsigned int j = 0; j < tx->vout.size(); j++) {

                // parse each token transaction
                CToken token;
                std::string strError;
                CScript TokenScript = tx->vout[j].scriptPubKey;
                if (!ContextualCheckToken(TokenScript, token, strError)) {
                    LogPrint(BCLog::TOKEN, "ContextualCheckToken returned with error %s\n", strError);
                    continue;
                }

                // check if it matches
                if (name == token.getName()) {
                    TokenSpend.hash = tx->GetHash();
                    TokenSpend.n = j;
                    return true;
                }
            }
        }
    }

    return false;
}

void UndoTokenIssuance(uint64_t& id, std::string& name)
{
    LOCK(cs_main);
    if (IsIdentifierInIssuances(id) && IsNameInIssuances(name)) {
        for (unsigned int index = 0; index < KnownIssuances.size(); index++) {
            uint64_t StoredId = KnownIssuances.at(index).getId();
            std::string StoredName = KnownIssuances.at(index).getName();
            if (StoredId == id && StoredName == name) {
                KnownIssuances.erase(KnownIssuances.begin() + index);
                return;
            }
        }
    }
}

void UndoTokenIssuancesInBlock(const CBlock& block)
{
    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        const CTransactionRef& tx = block.vtx[i];
        for (unsigned int j = 0; j < tx->vout.size(); j++) {
            CScript tokenData = tx->vout[j].scriptPubKey;
            if (tokenData.IsPayToToken()) {
                CToken token;
                if (token.isIssuance()) {
                    uint64_t id = token.getId();
                    std::string name = token.getName();
                    UndoTokenIssuance(id, name);
                }
            }
        }
    }
}
