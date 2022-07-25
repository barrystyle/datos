// Copyright (c) 2022 pacprotocol
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/signature.h>

#include <hash.h>
#include <keystore.h>
#include <primitives/block.h>
#include <util/system.h>

bool GetKeyIDFromUTXO(const CTxOut& txout, CKeyID& keyID)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType = Solver(txout.scriptPubKey, vSolutions);

    if (whichType == TX_PUBKEY) {
        keyID = CPubKey(vSolutions[0]).GetID();
    } else if (whichType == TX_PUBKEYHASH) {
        keyID = CKeyID(uint160(vSolutions[0]));
    } else {
        return false;
    }

    return true;
}

bool SignBlock(CBlock& block, const CKeyStore& keystore)
{
    CKeyID keyID;
    std::vector<valtype> vSolutions;
    const CTxOut& txout = block.IsProofOfStake() ? block.vtx[1]->vout[1] : block.vtx[0]->vout[0];

    if (!GetKeyIDFromUTXO(txout, keyID)) {
        return error("%s: failed to find key for %s", __func__, block.IsProofOfStake() ? "PoS" : "PoW");
    }

    CKey key;
    if (!keystore.GetKey(keyID, key)) {
        return error("%s: failed to get key from keystore", __func__);
    }

    if (!key.Sign(block.GetHash(), block.vchBlockSig)) {
        return error("%s: failed to sign block hash with key", __func__);
    }

    return true;
}

bool CheckBlockSignature(const CBlock& block)
{
    if (block.GetHash() == Params().GetConsensus().hashGenesisBlock) {
        return block.vchBlockSig.empty();
    }

    std::vector<valtype> vSolutions;
    const CTxOut& txout = block.IsProofOfStake() ? block.vtx[1]->vout[1] : block.vtx[0]->vout[0];

    if (Solver(txout.scriptPubKey, vSolutions) != TX_PUBKEY) {
        return false;
    }

    const valtype& vchPubKey = vSolutions[0];
    CPubKey key(vchPubKey);
    if (block.vchBlockSig.empty()) {
        return false;
    }

    return key.Verify(block.GetHash(), block.vchBlockSig);
}
