// Copyright (c) 2023 datos
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/issuances.h>

std::mutex IssuancesMutex;
std::vector<CToken> KnownIssuances;

void GetNextIssuanceId(uint64_t& id)
{
    id = ISSUANCE_ID_BEGIN + (GetRand(std::numeric_limits<uint64_t>::max() - ISSUANCE_ID_BEGIN));
}

bool IsIdentifierInMempool(uint64_t& id)
{
    LOCK(mempool.cs);

    //! check each mempool tx/vout's id if tokentx
    for (const auto& l : mempool.mapTx) {
        const CTransaction& mtx = l.GetTx();
        if (mtx.HasTokenOutput()) {
            for (unsigned int i = 0; i < mtx.vout.size(); i++) {
                CScript TokenScript = mtx.vout[i].scriptPubKey;
                if (TokenScript.IsPayToToken()) {
                    uint64_t tokenid;
                    if (!GetTokenidFromScript(TokenScript, tokenid)) {
                        continue;
                    }
                    if (tokenid == id) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool IsNameInIssuances(std::string& name)
{
    std::lock_guard<std::mutex> lock(IssuancesMutex);

    for (CToken& token : KnownIssuances) {
        if (token.getName() == name) {
            return true;
        }
    }
    return false;
}

bool IsIdentifierInIssuances(uint64_t& identifier)
{
    std::lock_guard<std::mutex> lock(IssuancesMutex);

    for (CToken& token : KnownIssuances) {
        if (token.getId() == identifier) {
            return true;
        }
    }
    return false;
}

bool GetIdForTokenName(std::string& name, uint64_t& id)
{
    std::lock_guard<std::mutex> lock(IssuancesMutex);

    for (CToken& token : KnownIssuances) {
        if (name == token.getName()) {
            id = token.getId();
            return true;
        }
    }
    return false;
}

std::vector<CToken> CopyIssuancesVector()
{
    std::lock_guard<std::mutex> lock(IssuancesMutex);

    std::vector<CToken> TempKnownIssuances = KnownIssuances;
    return TempKnownIssuances;
}

uint64_t GetIssuancesSize()
{
    std::lock_guard<std::mutex> lock(IssuancesMutex);

    return KnownIssuances.size();
}

void AddToIssuances(CToken& token)
{
    std::lock_guard<std::mutex> lock(IssuancesMutex);

    KnownIssuances.push_back(token);
}
