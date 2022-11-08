// Copyright (c) 2022 pacprotocol
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/signature.h>

#include <keystore.h>

bool SignBlockWithKey(CBlock& block, const CKey& key)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    const CTxOut& txout = block.vtx[1]->vout[1];

    whichType = Solver(txout.scriptPubKey, vSolutions);

    CKeyID keyID;

    if (whichType == TX_PUBKEYHASH)
        keyID = CKeyID(uint160(vSolutions[0]));
    else if (whichType == TX_PUBKEY)
        keyID = CPubKey(vSolutions[0]).GetID();

    if (!key.Sign(block.GetHash(), block.vchBlockSig)) return false;

    return true;
}

bool CheckBlockSignature(const CBlock& block)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    const CTxOut& txout = block.vtx[1]->vout[1];

    whichType = Solver(txout.scriptPubKey, vSolutions);
    valtype& vchPubKey = vSolutions[0];
    if (whichType == TX_PUBKEY)
    {
        CPubKey key(vchPubKey);
        if (block.vchBlockSig.empty()) return false;
        return key.Verify(block.GetHash(), block.vchBlockSig);
    }
    else if (whichType == TX_PUBKEYHASH)
    {
        CKeyID keyID;
        keyID = CKeyID(uint160(vchPubKey));
        CPubKey pubkey(vchPubKey);

        if (!pubkey.IsValid()) return false;
        if (block.vchBlockSig.empty()) return false;
        return pubkey.Verify(block.GetHash(), block.vchBlockSig);
    }

    return false;
}
