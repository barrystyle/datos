// Copyright (c) 2023 datos
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STORAGE_PREAUTH_H
#define STORAGE_PREAUTH_H

#include <base58.h>
#include <bls/bls.h>
#include <chainparams.h>
#include <evo/deterministicmns.h>
#include <masternode/node.h>
#include <netbase.h>
#include <validation.h>

#include <openssl/sha.h>

const int PREAUTH_PARTITION = 300;

bool PreauthGenerate(std::string& hexsig);
bool PreauthMockGenerate(std::string& hostAddress, CBLSSecretKey& sk, std::string& hexsig, CBLSPublicKey& pk);
bool PreauthVerify(std::string& hostAddress, std::string& hexsig, CBLSPublicKey& pk);

#endif // STORAGE_PREAUTH_H
