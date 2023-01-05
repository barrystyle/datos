// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STORAGE_PROOF_H
#define BITCOIN_STORAGE_PROOF_H

#include <net.h>
#include <storage/netproof.h>
#include <storage/serialize.h>
#include <storage/util.h>
#include <uint256.h>

#include <cstddef>
#include <list>
#include <type_traits>

// CProof: details of active storagenodes observed
class CProof {
public:
    std::vector<StorageNode> nodes;

    CProof()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CProof, obj)
    {
        READWRITE(obj.nodes);
    }

    void SetNull()
    {
        nodes.clear();
    }
};

#endif // BITCOIN_STORAGE_PROOF_H
