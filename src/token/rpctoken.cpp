// Copyright (c) 2023 datos
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <net.h>
#include <rpc/server.h>
#include <token/issuances.h>
#include <token/util.h>
#include <token/verify.h>
#include <validation.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>

UniValue tokendecode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "tokendecode \"script\"\n"
            "\nDecode a token script.\n"
            "\nArguments:\n"
            "1. \"script\"            (string, required) The token script to decode.\n");
    }

    // Script
    std::string scriptDecode = request.params[0].get_str();
    if (!scriptDecode.size()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid script length");
    }

    // Convert string to script
    CScript script;
    std::vector<unsigned char> scriptData(ParseHexV(request.params[0], "argument"));
    script = CScript(scriptData.begin(), scriptData.end());

    // Decode token into elements
    uint8_t version;
    uint16_t type;
    uint64_t identifier;
    std::string name;
    CPubKey ownerKey;
    DecodeTokenScript(script, version, type, identifier, name, ownerKey, true);

    // Decode destination
    CTxDestination dest;
    ExtractDestination(script, dest);

    // Print output
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("version", version);
    ret.pushKV("type", type);
    ret.pushKV("identifier", identifier);
    ret.pushKV("name", name);
    ret.pushKV("pubkey", EncodeDestination(dest));

    return ret;
}

UniValue tokenmint(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4) {
        throw std::runtime_error(
            "tokenmint \"address\" \"name\" amount \"checksum\"\n"
            "\nMint an amount of token, to a given address.\n"
            + HelpRequiringPassphrase() + "\nArguments:\n"
                                          "1. \"address\"            (string, required) The DTS address to send to.\n"
                                          "2. \"name\"               (string, required) The token name.\n"
                                          "3. \"amount\"             (numeric or string, required) The amount to mint.\n"
                                          "4. \"checksum\"           (string, optional) The checksum to associate with this token.\n"
                                          "\nResult:\n"
                                          "\"txid\"                  (string) The transaction id.\n"
                                          "\nExamples:\n"
            + HelpExampleCli("tokenmint", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" \"BAZ\" 100000")
            + HelpExampleRpc("tokenmint", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\", \"BAZ\", 10000"));
    }

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsUnlocked(pwallet);

    TokenSafetyChecks();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    // Address
    std::string strOwner = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(strOwner);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    // Name
    std::string strToken = request.params[1].get_str();

    std::string strError;
    StripControlChars(strToken);
    if (!CheckTokenName(strToken, strError)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid token name");
    }
    if (IsNameInIssuances(strToken)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Issuance name already used");
    }

    // Amount
    CAmount nAmount = AmountFromValue(request.params[2]) / COIN;
    if (nAmount < 1 || nAmount > TOKEN_VALUEMAX) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid token amount");
    }

    // Checksum
    std::string strChecksum;
    bool usingChecksum = true;
    if (request.params.size() == 4) {
        strChecksum = request.params[3].get_str();
        if (strChecksum.size() != 40 || !IsHex(strChecksum)) {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid checksum string specified");
        }
    } else {
        usingChecksum = false;
    }

    // Build script
    uint64_t identifier;
    CScript IssuanceScript;
    GetNextIssuanceId(identifier);
    CScript TokenDestination = GetScriptForDestination(dest);
    BuildTokenScript(IssuanceScript, CToken::CURRENT_VERSION, CToken::ISSUANCE, identifier, strToken, TokenDestination);

    // Build checksum script (if required)
    CScript ChecksumScript;
    if (usingChecksum) {
        std::vector<unsigned char> vecChecksum = ParseHex(strChecksum.c_str());
        uint160 checksum;
        memcpy(&checksum, vecChecksum.data(), 20);
        BuildChecksumScript(ChecksumScript, checksum);
    }

    // Extract balances from wallet
    CAmount valueOut;
    std::vector<CTxIn> RetInput;
    CAmount RequiredFunds = nAmount + (usingChecksum ? 1000 : 0);
    if (!pwallet->FundMintTransaction(RequiredFunds, valueOut, RetInput)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find enough token to create transaction.");
    }
    PrintTxinFunds(RetInput);

    // Generate new change address
    bool changeWasUsed = (valueOut - RequiredFunds) > 0;
    CPubKey newKey;
    CReserveKey reservekey(pwallet);
    {
       LOCK(pwallet->cs_wallet);
       if (!reservekey.GetReservedKey(newKey, true)) {
           throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
       }
    }
    CKeyID keyID = newKey.GetID();

    // Create transaction
    CMutableTransaction tx;
    tx.nLockTime = ::ChainActive().Height();
    tx.vin = RetInput;
    tx.vout.push_back(CTxOut(nAmount, IssuanceScript));

    if (usingChecksum) {
        tx.vout.push_back(CTxOut(1000, ChecksumScript));
    }

    if (changeWasUsed) {
        CAmount ChangeAmount = valueOut - RequiredFunds;
        CScript ChangeScript = GetScriptForDestination(keyID);
        tx.vout.push_back(CTxOut(ChangeAmount, ChangeScript));
    }

    // Sign transaction
    if (!pwallet->SignTokenTransaction(tx, strError)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Error signing token transaction (%s)", strError));
    }

    // Print tx for debug
    LogPrint(BCLog::TOKEN, "Constructed tx:\n%s\n", tx.ToString());

    // Check if it will be accepted
    bool res;
    CValidationState state;
    {
        LOCK(cs_main);
        res = AcceptToMemoryPool(mempool, state, MakeTransactionRef(tx), NULL, false, 0, true);
    }

    if (!res) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Transaction %s was constructed but not accepted by mempool (%s)", tx.GetHash().ToString(), state.GetDebugMessage()));
    }

    // Broadcast transaction
    std::string UnusedErrString;
    auto locked_chain = pwallet->chain().lock();
    CWalletTx wtx(pwallet, MakeTransactionRef(tx));
    if (!wtx.SubmitMemoryPoolAndRelay(UnusedErrString, false, *locked_chain)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error broadcasting token transaction");
    }

    // return change key if not used
    if (!changeWasUsed) {
        reservekey.ReturnKey();
    }

    return tx.GetHash().ToString();
}

UniValue tokenbalance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "tokenbalance \"name\"\n"
            "\nList received tokens and their amount.\n"
            "\nArguments:\n"
            "1. \"name\"            (string, optional) Only show tokens matching name.\n");

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsUnlocked(pwallet);

    TokenSafetyChecks();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    // Name
    bool use_filter = false;
    std::string filter_name;
    if (!request.params[0].isNull()) {
        use_filter = true;
        filter_name = request.params[0].get_str();
        StripControlChars(filter_name);
    } else {
        filter_name.clear();
    }

    std::map<std::string, CAmount> TokenBalancesConfirmed;
    std::map<std::string, CAmount> TokenBalancesUnconfirmed;

    // Iterate wallet txes
    std::string strError;
    UniValue result(UniValue::VOBJ);
    {
        auto locked_chain = pwallet->chain().lock();

        std::map<uint256, CWalletTx> walletInst;
        {
            LOCK(pwallet->cs_wallet);
            walletInst = pwallet->mapWallet;
        }

        for (auto it : walletInst) {

            const CWalletTx& wtx = it.second;
            if (wtx.IsCoinBase())
                continue;

            //! covers conflicted wtx's
            if (!wtx.IsTrusted(*locked_chain)) {
                continue;
            }

            uint256 txHash = wtx.tx->GetHash();
            for (int n = 0; n < wtx.tx->vout.size(); n++) {
                CTxOut out = wtx.tx->vout[n];
                CScript pk = out.scriptPubKey;
                CAmount nValue = out.nValue;

                //! dont count checksum output value
                if (pk.IsChecksumData()) {
                    continue;
                }

                //! wallet may show existing spent entries
                if (pwallet->IsSpent(*locked_chain, wtx.tx->GetHash(), n)) {
                    continue;
                }

                //! account for token in mempool, but not stale wallet sends
                bool inMempool = false;
                if (wtx.GetDepthInMainChain(*locked_chain) == 0) {
                    if (IsInMempool(txHash)) {
                        inMempool = true;
                    } else {
                        continue;
                    }
                }

                if (pk.IsPayToToken()) {
                    CToken token;
                    if (!BuildTokenFromScript(pk, token)) {
                        continue;
                    }
                    CTxDestination address;
                    ExtractDestination(pk, address);

                    //! make sure we only display items 'to' us
                    if (!IsMine(*pwallet, address)) {
                        continue;
                    }

                    //! create and fill entry
                    std::string name = token.getName();
                    if (!inMempool) {
                        TokenBalancesConfirmed[name] += nValue;
                    }
                }
            }
        }
    }

    pwallet->GetUnconfirmedTokenBalance(mempool, TokenBalancesUnconfirmed, strError);

    UniValue confirmed(UniValue::VOBJ);
    for (const auto& l : TokenBalancesConfirmed) {
        std::string name = l.first;
        if (!use_filter) {
            confirmed.pushKV(name, l.second);
        } else if (use_filter && CompareTokenName(filter_name, name)) {
            confirmed.pushKV(name, l.second);
        }
    }
    result.pushKV("confirmed", confirmed);

    UniValue unconfirmed(UniValue::VOBJ);
    for (const auto& l : TokenBalancesUnconfirmed) {
        std::string name = l.first;
        if (!use_filter) {
            unconfirmed.pushKV(name, l.second);
        } else if (use_filter && CompareTokenName(filter_name, name)) {
            unconfirmed.pushKV(name, l.second);
        }
    }
    result.pushKV("unconfirmed", unconfirmed);

    return result;
}

UniValue tokenlist(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "tokenlist\n"
            "\nList all token transactions in wallet.\n"
            "\nArguments:\n"
            "\nNone.\n");

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsUnlocked(pwallet);

    TokenSafetyChecks();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    // Get current height
    int height = ::ChainActive().Height();

    // Iterate wallet txes
    UniValue result(UniValue::VARR);
    {
        std::map<uint256, CWalletTx> walletInst;
        {
            LOCK(pwallet->cs_wallet);
            walletInst = pwallet->mapWallet;
        }

        for (auto it : walletInst) {

            const CWalletTx& wtx = it.second;

            uint256 wtxHash = wtx.GetHash();
            if (IsInMempool(wtxHash))
                continue;

            if (wtx.IsCoinBase())
                continue;

            for (int n = 0; n < wtx.tx->vout.size(); n++) {
                CTxOut out = wtx.tx->vout[n];
                CScript pk = out.scriptPubKey;
                CAmount nValue = out.nValue;

                if (pk.IsPayToToken()) {

                    CToken token;
                    if (!BuildTokenFromScript(pk, token)) {
                        continue;
                    }
                    CTxDestination address;
                    ExtractDestination(pk, address);

                    //! wtx_type false (received), true (sent)
                    bool wtx_type = false;
                    if (!IsMine(*pwallet, address)) {
                        wtx_type = true;
                    }

                    //! create and fill entry
                    std::string name = token.getName();

                    UniValue entry(UniValue::VOBJ);
                    entry.pushKV("token", name);
                    entry.pushKV("address", EncodeDestination(address));
                    entry.pushKV("category", wtx_type ? "send" : "receive");
                    entry.pushKV("amount", nValue);
                    if (!::BlockIndex()[wtx.hashBlock]) {
                        entry.pushKV("confirmations", -1);
                    } else {
                        entry.pushKV("confirmations", height - ::BlockIndex()[wtx.hashBlock]->nHeight);
                    }
                    entry.pushKV("time", wtx.GetTxTime());
                    entry.pushKV("block", wtx.hashBlock.ToString());
                    UniValue outpoint(UniValue::VOBJ);
                    outpoint.pushKV(wtx.tx->GetHash().ToString(), n);
                    entry.pushKV("outpoint", outpoint);

                    result.push_back(entry);
                }
            }
        }
    }

    return result;
}

UniValue tokensend(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3) {
        throw std::runtime_error(
            "tokensend \"address\" \"name\" amount\n"
            "\nSend an amount of token, to a given address.\n"
            + HelpRequiringPassphrase() + "\nArguments:\n"
                                          "1. \"address\"            (string, required) The DTS address to send to.\n"
                                          "2. \"name\"               (string, required) The token name.\n"
                                          "3. \"amount\"             (numeric or string, required) The amount to send.\n"
                                          "\nResult:\n"
                                          "\"txid\"                  (string) The transaction id.\n"
                                          "\nExamples:\n"
            + HelpExampleCli("tokensend", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" \"BAZ\" 100000")
            + HelpExampleRpc("tokensend", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\", \"BAZ\", 10000"));
    }

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsUnlocked(pwallet);

    TokenSafetyChecks();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    // Address
    std::string strDest = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(strDest);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    // Name
    std::string strToken = request.params[1].get_str();
    StripControlChars(strToken);
    if (strToken.size() < TOKENNAME_MINLEN || strToken.size() > TOKENNAME_MAXLEN) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid token name");
    }

    // Amount
    CAmount nAmount = AmountFromValue(request.params[2]) / COIN;
    if (nAmount < 1 || nAmount > TOKEN_VALUEMAX) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid token amount");
    }

    // Extract token/balances from wallet
    CAmount valueOut;
    std::vector<CTxIn> RetInput;
    if (!pwallet->FundTokenTransaction(strToken, nAmount, valueOut, RetInput)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find enough token to create transaction.");
    }
    PrintTxinFunds(RetInput);

    // Generate target destination 'out'
    CScript destPubKey;
    CScript destScript = GetScriptForDestination(dest);
    uint64_t id;
    if (!GetIdForTokenName(strToken, id)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find token id from returned token inputs.");
    }
    BuildTokenScript(destPubKey, CToken::CURRENT_VERSION, CToken::TRANSFER, id, strToken, destScript);
    CTxOut destOutput(nAmount, destPubKey);

    // Generate new change address
    bool changeWasUsed = false;
    CPubKey newKey;
    CReserveKey reservekey(pwallet);
    {
       LOCK(pwallet->cs_wallet);
       if (!reservekey.GetReservedKey(newKey, true)) {
           throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
       }
    }
    CKeyID keyID = newKey.GetID();

    // Create transaction
    CMutableTransaction tx;
    tx.nLockTime = ::ChainActive().Height();
    tx.vin = RetInput;
    tx.vout.push_back(destOutput);

    // Generate target change 'out'
    if (valueOut - nAmount > 0) {
        CScript destChangePubKey;
        CScript destChangeScript = GetScriptForDestination(keyID);
        BuildTokenScript(destChangePubKey, CToken::CURRENT_VERSION, CToken::TRANSFER, id, strToken, destChangeScript);
        CTxOut destChangeOutput(valueOut - nAmount, destChangePubKey);
        tx.vout.push_back(destChangeOutput);
        changeWasUsed = true;
    }

    // Sign transaction
    std::string strError;
    if (!pwallet->SignTokenTransaction(tx, strError)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Error signing token transaction (%s)", strError));
    }

    // Print tx for debug
    LogPrint(BCLog::TOKEN, "Constructed tx:\n%s\n", tx.ToString());

    // Check if it will be accepted
    bool res;
    CValidationState state;
    {
        LOCK(cs_main);
        res = AcceptToMemoryPool(mempool, state, MakeTransactionRef(tx), NULL, false, 0, true);
    }

    if (!res) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Transaction %s was constructed but not accepted by mempool (%s)", tx.GetHash().ToString(), state.GetDebugMessage()));
    }

    // Broadcast transaction
    std::string UnusedErrString;
    auto locked_chain = pwallet->chain().lock();
    CWalletTx wtx(pwallet, MakeTransactionRef(tx));
    if (!wtx.SubmitMemoryPoolAndRelay(UnusedErrString, false, *locked_chain)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error broadcasting token transaction");
    }

    // return change key if not used
    if (!changeWasUsed) {
        reservekey.ReturnKey();
    }

    return tx.GetHash().ToString();
}

UniValue tokenissuances(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "tokenissuances\n"
            "\nList known token issuances.\n"
            "\nArguments:\n"
            "none\n");
    }

    TokenSafetyChecks();

    UniValue issuances(UniValue::VOBJ);
    {
        LOCK(cs_main);
        for (CToken& token : KnownIssuances) {
            UniValue issuance(UniValue::VOBJ);
            issuance.pushKV("version", strprintf("%02x", token.getVersion()));
            issuance.pushKV("type", strprintf("%04x", token.getType()));
            issuance.pushKV("identifier", strprintf("%016x", token.getId()));
            issuance.pushKV("origintx", token.getOriginTx().ToString());
            issuances.pushKV(token.getName(), issuance);
        }
    }

    return issuances;
}

UniValue tokenchecksum(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "tokenchecksum \"name\"\n"
            "\nRetrieve checksum hash for a given token.\n"
            "\nArguments:\n"
            "1. \"name\"            (string, required) The token to retrieve checksum from.\n");
    }

    TokenSafetyChecks();

    // Name
    std::string strToken = request.params[0].get_str();
    StripControlChars(strToken);
    if (strToken.size() < TOKENNAME_MINLEN || strToken.size() > TOKENNAME_MAXLEN) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid token name");
    }

    // Search and retrieve checksum
    {
        LOCK(cs_main);
        for (CToken& token : KnownIssuances) {
            if (strToken == token.getName()) {
                //! fetch token origin tx
                uint256 blockHash;
                CTransactionRef tx;
                uint256 origin = token.getOriginTx();
                if (!GetTransaction(origin, tx, Params().GetConsensus(), blockHash)) {
                    throw JSONRPCError(RPC_TYPE_ERROR, "Could not retrieve token origin transaction.");
                }
                //! fetch checksum output
                for (unsigned int i = 0; i < tx->vout.size(); i++) {
                    if (tx->vout[i].IsTokenChecksum()) {
                        uint160 ChecksumOutput;
                        CScript ChecksumScript = tx->vout[i].scriptPubKey;
                        if (!DecodeChecksumScript(ChecksumScript, ChecksumOutput)) {
                            throw JSONRPCError(RPC_TYPE_ERROR, "Could not retrieve checksum from token origin transaction.");
                        }
                        return HexStr(ChecksumOutput);
                    }
                }
            }
        }
    }

    return NullUniValue;
}

UniValue tokenhistory(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "tokenhistory \"name\"\n"
            "\nFind latest token of type name and trace it all the way back to issuance.\n"
            "\nArguments:\n"
            "1. \"name\"            (string, required) The token to display history for.\n");
    }

    TokenSafetyChecks();

    // Get current height
    int height = ::ChainActive().Height();

    // Name
    std::string strToken = request.params[0].get_str();
    StripControlChars(strToken);
    if (strToken.size() < TOKENNAME_MINLEN || strToken.size() > TOKENNAME_MAXLEN) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid token name");
    }

    // Retrieve token history
    UniValue history(UniValue::VARR);
    {
        LOCK(cs_main);
        COutPoint TokenSpend;
        if (!FindLastTokenUse(strToken, TokenSpend, height, Params().GetConsensus())) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to find usage of token");
        }

        uint256 hash = TokenSpend.hash;
        int n = TokenSpend.n;

        while (true) {

            // fetch transaction
            uint256 blockHash;
            CTransactionRef tx;
            if (!GetTransaction(hash, tx, Params().GetConsensus(), blockHash)) {
                throw JSONRPCError(RPC_TYPE_ERROR, "Could not retrieve token transaction.");
            }

            // decode token
            CToken token;
            std::string strError;
            CScript TokenScript = tx->vout[n].scriptPubKey;
            if (!ContextualCheckToken(TokenScript, token, strError)) {
                LogPrint(BCLog::TOKEN, "ContextualCheckToken returned with error %s\n", strError);
                throw JSONRPCError(RPC_TYPE_ERROR, "Token data inconsistent.");
            }

            // add entry to history
            UniValue entry(UniValue::VOBJ);
            entry.pushKV("name", strToken);
            entry.pushKV("type", token.isIssuance() ? "issuance" : "transfer");
            entry.pushKV("amount", tx->vout[n].nValue);
            entry.pushKV("height", ::BlockIndex()[blockHash]->nHeight);
            UniValue outpoint(UniValue::VOBJ);
            outpoint.pushKV(hash.ToString(), n);
            entry.pushKV("outpoint", outpoint);
            history.push_back(entry);

            // check when to bail
            if (token.isIssuance()) {
                break;
            }

            // check token
            if (strToken != token.getName()) {
                throw JSONRPCError(RPC_TYPE_ERROR, "Token data inconsistent.");
            }

            // get prevout for token
            for (unsigned int i = 0; tx->vin.size(); i++) {
                hash = tx->vin[i].prevout.hash;
                n = tx->vin[i].prevout.n;
                break;
            }
        }
    }

    return history;
}

UniValue tokeninfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "tokeninfo \"name\"\n"
            "\nOutputs token's information.\n"
            "\nArguments:\n"
            "1. \"name\"            (string, required) The token to show information.\n");
    }

    TokenSafetyChecks();

    // Name
    std::string strToken = request.params[0].get_str();
    StripControlChars(strToken);
    if (strToken.size() < TOKENNAME_MINLEN || strToken.size() > TOKENNAME_MAXLEN) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid token name");
    }

    // Search and retrieve token and checksum
    UniValue result(UniValue::VOBJ);
    {
        LOCK(cs_main);
        for (CToken& token : KnownIssuances) {
            if (strToken == token.getName()) {
                //! fetch token origin tx
                uint256 blockHash;
                CTransactionRef tx;
                uint256 origin_tx = token.getOriginTx();
                if (!GetTransaction(origin_tx, tx, Params().GetConsensus(), blockHash)) {
                    throw JSONRPCError(RPC_TYPE_ERROR, "Could not retrieve token origin transaction.");
                }

                UniValue entry(UniValue::VOBJ);
                entry.pushKV("version", strprintf("%02x", token.getVersion()));
                entry.pushKV("type", strprintf("%04x", token.getType()));
                entry.pushKV("identifier", strprintf("%016x", token.getId()));

                UniValue origin(UniValue::VOBJ);
                origin.pushKV("tx", token.getOriginTx().ToString());

                //! fetch token and checksum output from origin transactions
                bool FoundToken = false;
                bool FoundChecksum = false;
                for (unsigned int i = 0; i < tx->vout.size(); i++) {
                    if (tx->vout[i].IsTokenOutput()) {
                        uint8_t version;
                        uint16_t type;
                        uint64_t identifier;
                        std::string name;
                        CPubKey ownerKey;
                        CScript TokenScript = tx->vout[i].scriptPubKey;
                        if (!DecodeTokenScript(TokenScript, version, type, identifier, name, ownerKey, true)) {
                            throw JSONRPCError(RPC_TYPE_ERROR, "Could not retrieve token from origin transaction.");
                        }
                        CTxDestination address;
                        ExtractDestination(TokenScript, address);
                        CAmount amount = tx->vout[i].nValue;
                        origin.pushKV("address", EncodeDestination(address));
                        origin.pushKV("maxsupply", amount);
                        FoundToken = true;
                        if (FoundToken && FoundChecksum) {
                            break;
                        }
                    }
                    if (tx->vout[i].IsTokenChecksum()) {
                        uint160 ChecksumOutput;
                        CScript ChecksumScript = tx->vout[i].scriptPubKey;
                        if (!DecodeChecksumScript(ChecksumScript, ChecksumOutput)) {
                            throw JSONRPCError(RPC_TYPE_ERROR, "Could not retrieve checksum from token origin transaction.");
                        }
                        entry.pushKV("checksum", HexStr(ChecksumOutput));
                        FoundChecksum = true;
                        if (FoundToken && FoundChecksum) {
                            break;
                        }
                    }
                }

                entry.pushKV("origin", origin);
                result.pushKV(token.getName(), entry);

                return result;
            }
        }
    }

    return NullUniValue;
}

UniValue tokenunspent(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "tokenunspent\n"
            "\nList all unspent token outputs.\n");

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsUnlocked(pwallet);

    TokenSafetyChecks();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    // Iterate wallet txes
    UniValue result(UniValue::VARR);
    {
        auto locked_chain = pwallet->chain().lock();

        std::map<uint256, CWalletTx> walletInst;
        {
            LOCK(pwallet->cs_wallet);
            walletInst = pwallet->mapWallet;
        }

        for (auto it : walletInst) {

            const CWalletTx& wtx = it.second;
            if (wtx.IsCoinBase())
                continue;

            if (!::BlockIndex()[wtx.hashBlock])
                continue;

            if (!wtx.IsTrusted(*locked_chain))
                continue;

            uint256 txHash = wtx.tx->GetHash();
            for (int n = 0; n < wtx.tx->vout.size(); n++) {
                CTxOut out = wtx.tx->vout[n];
                CScript pk = out.scriptPubKey;
                CAmount nValue = out.nValue;

                //! wallet may show existing spent entries
                if (pwallet->IsSpent(*locked_chain, wtx.tx->GetHash(), n)) {
                    continue;
                }

                if (pk.IsPayToToken()) {
                    CToken token;
                    if (!BuildTokenFromScript(pk, token)) {
                        continue;
                    }

                    CTxDestination address;
                    ExtractDestination(pk, address);

                    //! make sure we only display items 'to' us
                    if (!IsMine(*pwallet, address)) {
                        continue;
                    }

                    //! create and fill entry
                    UniValue entry(UniValue::VOBJ);
                    if (nValue > 0) {
                        entry.pushKV("token", token.getName());
                        entry.pushKV("data", HexStr(pk));
                        entry.pushKV("amount", nValue);
                        result.push_back(entry);
                    }
                }
            }
        }
    }

    return result;
}

static const CRPCCommand commands[] = {
    //  category              name                      actor (function)
    //  --------------------- ------------------------  -----------------------
    { "token", "tokendecode", &tokendecode, { "script" } },
    { "token", "tokenmint", &tokenmint, { "address", "name", "amount", "checksum" } },
    { "token", "tokenbalance", &tokenbalance, { "name" } },
    { "token", "tokenhistory", &tokenhistory, { "name" } },
    { "token", "tokenlist", &tokenlist, {} },
    { "token", "tokensend", &tokensend, { "address", "name", "amount" } },
    { "token", "tokenissuances", &tokenissuances, {} },
    { "token", "tokenchecksum", &tokenchecksum, { "name" } },
    { "token", "tokeninfo", &tokeninfo, { "name" } },
    { "token", "tokenunspent", &tokenunspent, {} },
};

void RegisterTokenRPCCommands(CRPCTable& tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
