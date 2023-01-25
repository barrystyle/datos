// Copyright (c) 2022 pacprotocol/barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <net_processing.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <rpc/util.h>
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
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            RPCHelpMan{"resubmitproof",
                "\nResubmit the last network proof to the network.\n",
                {},
                RPCResults{},
                RPCExamples{""},
            }.ToString());

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
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            RPCHelpMan{"submitproof",
                "\nSubmit a network proof to be signed and broadcast to the network.\n",
                {
                    {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded netproof"},
                    {"privatekey", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "private key in base58-encoding"},
                },
                RPCResult{
                    RPCResult::Type::STR_HEX, "", "The netproof hash"},
                RPCExamples{
                    HelpExampleCli("submitproof", "030000000600000042284e05010101001000..")
            + HelpExampleRpc("submitproof", "030000000600000042284e05010101001000..")
                },
            }.ToString());

    UniValue result(UniValue::VOBJ);

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

static UniValue listproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            RPCHelpMan{"listproof",
                "\nReturn a list of the most recent proofs seen on the network.\n",
                {},
                RPCResults{},
                RPCExamples{""},
            }.ToString());

    UniValue result(UniValue::VOBJ);

    LOCK(cs_main);
    const int height = ::ChainActive().Height();

    // sum the proofs we have
    int count = 0;
    for (auto l : proofs) count++;
    if (count == 0) {
        return result;
    }

    // only display the last 50
    for (auto l : proofs) {
        if (l.height > height - 50) {
            result.pushKV(std::to_string(l.height), l.hash.ToString());
        }
    }

    return result;
}

static UniValue parseproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            RPCHelpMan{"parseproof",
                "\nParse the most recent proof to get the current network storage capacity (in Gb).\n",
                {},
                RPCResults{},
                RPCExamples{""},
            }.ToString());

    UniValue result(UniValue::VOBJ);

    // sum the proofs we have
    int count = 0;
    for (auto l : proofs) count++;
    if (count == 0) {
        return result;
    }

    // most recent
    CNetworkProof& netproof = proofs.back();
    CProof& proof = netproof.proof;
    int totalStorage = 0;
    for (auto l : proof.nodes) {
        totalStorage += l.space;
    }

    result.pushKV("height", netproof.height);
    result.pushKV("storage", totalStorage);

    return result;
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)
  //  --------------------- ------------------------  -----------------------
    { "storage",            "resubmitproof",          &resubmitproof,          {}  },
    { "storage",            "submitproof",            &submitproof,            {"hexstring", "privatekey"}  },
    { "storage",            "listproof",              &listproof,              {}  },
    { "storage",            "parseproof",             &parseproof,             {}  },
};

// clang-format on
void RegisterStorageRPCCommands(CRPCTable& tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
