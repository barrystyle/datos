// Copyright (c) 2022 pacprotocol/barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <net_processing.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <storage/manager.h>
#include <storage/netproof.h>
#include <storage/proof.h>
#include <storage/serialize.h>
#include <streams.h>
#include <util/strencodings.h>
#include <validation.h>
#include <version.h>

#include <univalue.h>

const unsigned int proof_string_sz = 1048576;

bool DecodeHexProof(CProof& proof, const std::string& strHexProof)
{
    if (!IsHex(strHexProof)) {
        return false;
    }

    try {
        uint32_t cont_sz = strHexProof.size();
        if (cont_sz > proof_string_sz) {
            return false;
        }
        char proofBin[proof_string_sz];
        std::vector<unsigned char> proofData(ParseHex(strHexProof));
        memcpy(proofBin, proofData.data(), cont_sz);
        uint32_t elem_sz = get_elements_in_container(proofBin);

        proof.nodes.clear();
        for (uint32_t i = 0; i < elem_sz; i++) {
            struct StorageNode out = unpack_obj_from_container(proofBin, i);
            proof.nodes.push_back(out);
        }
    } catch (const std::exception&) {
        return false;
    }

    return true;
}

static UniValue resubmitproof(const JSONRPCRequest& request)
{
    UniValue result(UniValue::VOBJ);

    if (proofs.size() < 1) {
        return result;
    }

    CNetworkProof netproof = proofs.back();
    netproof.Relay(*g_connman);

    return result;
}

static UniValue submitproof(const JSONRPCRequest& request)
{
    UniValue result(UniValue::VOBJ);

    if (request.params.size() < 2) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Proof and private key required");
    }

    std::string hexProof = request.params[0].get_str();
    if (hexProof.size() < 2 * MIN_PROOF_SZ) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Proof under min size");
    }

    std::string privKey = request.params[1].get_str();
    if (privKey.size() < 34) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    }

    CKey key = DecodeSecret(privKey);
    if (!key.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    }

    CProof proof;
    if (!DecodeHexProof(proof, hexProof)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Proof decode failed");
    }

    CNetworkProof netproof;
    {
        LOCK(cs_main);
        netproof.height = ::ChainActive().Height() + 1;
        netproof.proof = proof;
        netproof.CalculateHash();
        key.SignCompact(netproof.hash, netproof.vchProofSig);
    }

    proofs.push_back(netproof);
    result.push_back(netproof.hash.ToString());
    netproof.Relay(*g_connman);

    return result;
}

static UniValue listmemproof(const JSONRPCRequest& request)
{
    int count, lcount;
    UniValue result(UniValue::VOBJ);

    LOCK(cs_main);

    // sum the proofs we have
    count = 0;
    for (auto l : proofs)
        count++;

    // only display the last 50
    lcount = 0;
    for (auto l : proofs) {
        lcount++;
        if (lcount > count - 50) {
            result.pushKV(std::to_string(l.height), l.hash.ToString());
        }
    }

    return result;
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)
  //  --------------------- ------------------------  -----------------------
    { "storage",            "resubmitproof",          &resubmitproof,          {}  },
    { "storage",            "submitproof",            &submitproof,            {}  },
    { "storage",            "listmemproof",           &listmemproof,           {}  }
};

// clang-format on
void RegisterStorageRPCCommands(CRPCTable& tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
