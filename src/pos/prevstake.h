// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2022 datosdrive
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef POS_STAKESEEN_H
#define POS_STAKESEEN_H

#include <primitives/transaction.h>
#include <validation.h>

/** Functions for validating blocks and updating the block tree */
bool CheckStakeUnused(const COutPoint &kernel);
bool CheckStakeUnique(const CBlock &block, bool fUpdate=true);

#endif // POS_STAKESEEN_H
