// Copyright (c) 2021 datosdrive
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOKEN_UTIL_H
#define TOKEN_UTIL_H

#include <amount.h>
#include <key_io.h>
#include <logging.h>
#include <rpc/util.h>
#include <script/script.h>
#include <serialize.h>
#include <token/token.h>
#include <token/verify.h>
#include <txmempool.h>
#include <util/strencodings.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <boost/algorithm/string.hpp>

class CToken;
class CTxMemPool;

void TokenSafetyChecks();
bool AreTokensActive(int height = 0);
void ReclaimInvalidInputs();
bool CompareTokenName(std::string& PrevTokenName, std::string& TokenName);
bool CheckTokenName(std::string& tokenName, std::string& errorReason);
void StripControlChars(std::string& instr);
bool IsInMempool(uint256& txhash);
void RemoveFromMempool(CTransaction& tx);
bool IsOutputUnspent(const COutPoint& out);
bool IsOutputInMempool(const COutPoint& out);
int TokentxInMempool();
void PrintTxinFunds(std::vector<CTxIn>& FundsRet);
opcodetype GetOpcode(int n);
int GetIntFromOpcode(opcodetype n);

#endif // TOKEN_UTIL_H
