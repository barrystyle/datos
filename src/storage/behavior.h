// Copyright (c) 2022 pacprotocol/barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STORAGE_BEHAVIOR_H
#define BITCOIN_STORAGE_BEHAVIOR_H

#include <chainparams.h>
#include <evo/deterministicmns.h>
#include <util/system.h>
#include <validation.h>

class CNodeBehavior;
extern CNodeBehavior scoreManager;

// per node
struct NodeHistory {
    uint32_t ipaddr;
    int space;
    int health;
};

// all nodes
class CNodeBehavior {
public:
    const int SCORE_INCREASE = 5;
    const int SCORE_DECREASE = 25;

private:
    std::vector<int> seen;
    std::vector<NodeHistory> nodes;

public:
    void Init(const Consensus::Params& params);
    void SetSeen(int height);
    bool HaveSeen(int height);
    bool HaveNode(uint32_t ipaddr);
    void AddNode(struct NodeHistory in);
    bool IsValidDMN(uint32_t ip, const CChainParams& chainparams = Params());
    void AddProof(const CNetworkProof& netproof);
    bool ReplaceNode(struct NodeHistory in);
    struct NodeHistory ReturnNode(uint32_t ipaddr);
    void GetNodeScore(CService& mnAddress, int& score, int& space);
};

#endif // BITCOIN_STORAGE_BEHAVIOR_H
