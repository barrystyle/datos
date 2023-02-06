// Copyright (c) 2022 datosdrive/barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <storage/behavior.h>

#include <chainparams.h>
#include <hash.h>
#include <key_io.h>
#include <logging.h>
#include <netbase.h>
#include <protocol.h>
#include <pubkey.h>
#include <streams.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>

CNodeBehavior scoreManager;

void CNodeBehavior::Init(const Consensus::Params& params)
{
    nodes.clear();

    LOCK(cs_main);
    const int start_height = ::ChainActive().Height();
    if (start_height <= params.nLastPoWBlock) {
        return;
    }

    for (int height = params.nLastPoWBlock + 1; height <= start_height; height++) {
        const CBlockIndex* pindex = ::ChainActive()[height];
        CBlock block;
        ReadBlockFromDisk(block, pindex, params);
        AddProof(block.netProof);
    }
}

void CNodeBehavior::SetSeen(int height)
{
    for (auto& l : seen) {
        if (l == height)
            return;
    }
    seen.push_back(height);
}

bool CNodeBehavior::HaveSeen(int height)
{
    for (auto& l : seen) {
        if (l == height)
            return true;
    }
    return false;
}

bool CNodeBehavior::HaveNode(uint32_t ipaddr)
{
    for (auto& l : nodes) {
        if (l.ipaddr == ipaddr)
            return true;
    }
    return false;
}

struct NodeHistory CNodeBehavior::ReturnNode(uint32_t ipaddr)
{
    for (auto& l : nodes) {
        if (l.ipaddr == ipaddr)
            return l;
    }
    // shouldnt hit here
    struct NodeHistory blank;
    return blank;
}

bool CNodeBehavior::ReplaceNode(struct NodeHistory in)
{
    for (auto& l : nodes) {
        if (l.ipaddr == in.ipaddr) {
            l = in;
            return true;
        }
    }
    return false;
}

void CNodeBehavior::AddNode(struct NodeHistory in)
{
    nodes.push_back(in);
}

bool CNodeBehavior::IsValidDMN(uint32_t ip, const CChainParams& chainparams)
{
    CService addr;
    char ipaddress[21];
    uint32_to_ip(ip, ipaddress, chainparams.GetDefaultPort());
    if (!Lookup(ipaddress, addr, 0, false)) return false;
    if (!deterministicMNManager->GetListAtChainTip().GetMNByService(addr)) return false;
    if (deterministicMNManager->GetListAtChainTip().GetMNByService(addr)->pdmnState->IsBanned()) return false;
    return true;
}

void CNodeBehavior::AddProof(const CNetworkProof& netproof)
{
    int height;
    CProof proof;
    char ipaddress[16];
    struct NodeHistory in2;
    std::vector<uint32_t> seen_nodes;

    height = netproof.height;
    if (HaveSeen(height)) {
        return;
    }

    proof = netproof.proof;
    for (struct StorageNode& in : proof.nodes)
    {
        if (!HaveNode(in.ip)) {
            in2.ipaddr = in.ip;
            in2.space = in.space;
            in2.health = 100;
            AddNode(in2);
        }

        if (HaveNode(in.ip)) {
            in2 = ReturnNode(in.ip);
            if (in.mode > 0 && in.stat > 0 && in.reg > 0) {
                seen_nodes.push_back(in.ip);
                in2.health += SCORE_INCREASE;
            }
            if (in.space != in2.space) {
                in2.space = in.space;
                in2.health = 0;
            }
            if (in2.health > 100) {
                in2.health = 100;
            }
            uint32_to_ip(in2.ipaddr, ipaddress);
            if (IsValidDMN(in2.ipaddr)) {
                LogPrint(BCLog::STORAGE, "%s: height %d, ip %s, score %d (dmn)\n", __func__, height, ipaddress, in2.health);
            } else {
                LogPrint(BCLog::STORAGE, "%s: height %d, ip %s, score %d (unknown)\n", __func__, height, ipaddress, in2.health);
            }
            ReplaceNode(in2);
        }
    }

    for (auto& l : nodes)
    {
         if (std::find(seen_nodes.begin(), seen_nodes.end(), l.ipaddr) != seen_nodes.end()) {
             continue;
         }
         l.health -= SCORE_DECREASE;
         if (l.health < 0) {
             l.health = 0;
         }
         uint32_to_ip(l.ipaddr, ipaddress);
         if (IsValidDMN(l.ipaddr)) {
             LogPrint(BCLog::STORAGE, "%s: height %d, ip %s, score %d (dmn)\n", __func__, height, ipaddress, l.health);
         } else {
             LogPrint(BCLog::STORAGE, "%s: height %d, ip %s, score %d (unknown)\n", __func__, height, ipaddress, l.health);
         }
    }
}

void CNodeBehavior::GetNodeScore(CService& mnAddress, int& score, int& space)
{
    std::string mnAddressString = mnAddress.ToStringIP();

    for (auto& l : nodes) {
         char ipaddress[16];
         uint32_to_ip(l.ipaddr, ipaddress);
         std::string nodeAddressString = std::string(ipaddress);
         if (mnAddressString.compare(nodeAddressString) == 0) {
             score = l.health;
             space = l.space;
             return;
         }
    }

    score = 0;
    space = 0;
    return;
}
