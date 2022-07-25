// Copyright (c) 2022 pacprotocol
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef POS_SIGNATURE_H
#define POS_SIGNATURE_H

#include <chainparams.h>
#include <primitives/transaction.h>
#include <primitives/block.h>
#include <keystore.h>

using valtype = std::vector<unsigned char>;

bool GetKeyIDFromUTXO(const CTxOut& txout, CKeyID& keyID);
bool SignBlock(CBlock& block, const CKeyStore& keystore);
bool CheckBlockSignature(const CBlock& block);

#endif // POS_SIGNATURE_H
