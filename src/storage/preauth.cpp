// Copyright (c) 2023 datos
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <storage/preauth.h>

uint256 GenerateAuthHash(std::string pubKeyString, std::string& hostAddress)
{
    int64_t timePartition = GetAdjustedTime() / PREAUTH_PARTITION;
    std::string partString = std::to_string(timePartition);
    std::string authPhrase = string_with_padding(partString, 16) + string_with_padding(hostAddress, 16) + string_with_padding(pubKeyString, 96);

    uint256 hash;
    SHA256((const unsigned char*)authPhrase.c_str(), authPhrase.size(), hash.data());
    return hash;
}

bool PreauthGenerate(std::string& hexsig)
{
    if (!fMasternodeMode) {
        return false;
    }

    std::string hostAddress;
    for (const std::string& strAddr : gArgs.GetArgs("-externalip")) {
        hostAddress = strAddr;
        break;
    }

    LogPrint(BCLog::STORAGE, "%s: regenerating preauth for node %s\n", __func__, hostAddress);

    const auto& pubKey = activeMasternodeInfo.blsKeyOperator->GetPublicKey();
    uint256 hash = GenerateAuthHash(pubKey.ToString(), hostAddress);

    // generate signature
    const CBLSSignature& signature = activeMasternodeInfo.blsKeyOperator->Sign(hash);
    if (!signature.IsValid()) {
        LogPrint(BCLog::STORAGE, "%s: invalid signature\n", __func__);
        return false;
    }

    hexsig = signature.ToString();
    return true;
}

bool PreauthMockGenerate(std::string& hostAddress, CBLSSecretKey& sk, std::string& hexsig, CBLSPublicKey& pk)
{
    pk = sk.GetPublicKey();
    uint256 hash = GenerateAuthHash(pk.ToString(), hostAddress);

    // generate signature
    const CBLSSignature& signature = sk.Sign(hash);
    if (!signature.IsValid()) {
        LogPrint(BCLog::STORAGE, "%s: invalid signature\n", __func__);
        return false;
    }

    hexsig = signature.ToString();
    return true;
}

bool PreauthVerify(std::string& hostAddress, std::string& hexsig, CBLSPublicKey& pk)
{
    auto mnList = deterministicMNManager->GetListAtChainTip();
    CService authService(LookupNumeric(hostAddress.c_str()), Params().GetDefaultPort());
    CDeterministicMNCPtr dmn = mnList.GetMNByService(authService);
    if (pk == CBLSPublicKey()) {
        if (!dmn) {
            LogPrint(BCLog::STORAGE, "%s: dmn doesn't exist\n", __func__);
            return false;
        } else if (!mnList.IsMNValid(*dmn) || mnList.IsMNPoSeBanned(*dmn)) {
            LogPrint(BCLog::STORAGE, "%s: dmn is invalid or banned\n", __func__);
            return false;
        }
    } else {
        LogPrint(BCLog::STORAGE, "%s: mock pk detected\n", __func__);
    }

    // if pk is new uninit object, use dmn pubkey.. else use provided pk
    CBLSPublicKey pubKey = pk == CBLSPublicKey() ? dmn->pdmnState->pubKeyOperator.Get() : pk;
    uint256 hash = GenerateAuthHash(pubKey.ToString(), hostAddress);

    // reserialize signature
    CBLSSignature signature;
    signature.SetHexStr(hexsig);
    if (!signature.IsValid()) {
        LogPrint(BCLog::STORAGE, "%s: invalid signature\n", __func__);
        return false;
    }

    // verify signature
    if (!signature.VerifyInsecure(pubKey, hash)) {
        LogPrint(BCLog::STORAGE, "%s: signature doesnt match\n", __func__);
        return false;
    }

    return true;
}
