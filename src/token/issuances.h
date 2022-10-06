// Copyright (c) 2021 pacprotocol
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOKEN_ISSUANCES_H
#define TOKEN_ISSUANCES_H

#include <amount.h>
#include <key_io.h>
#include <logging.h>
#include <script/script.h>
#include <serialize.h>
#include <token/token.h>
#include <token/verify.h>
#include <txmempool.h>
#include <util/strencodings.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <mutex>

#include <boost/algorithm/string.hpp>

class CToken;
class CTxMemPool;

const int ISSUANCE_ID_BEGIN = 16;
extern std::vector<CToken> KnownIssuances;

void GetNextIssuanceId(uint64_t& id);
bool IsIdentifierInMempool(uint64_t& id);
bool IsNameInIssuances(std::string& name);
bool IsIdentifierInIssuances(uint64_t& identifier);
bool GetIdForTokenName(std::string& name, uint64_t& id);
std::vector<CToken> CopyIssuancesVector();
uint64_t GetIssuancesSize();
void AddToIssuances(CToken& token);

#endif // TOKEN_ISSUANCES_H
