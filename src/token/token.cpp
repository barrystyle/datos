// Copyright (c) 2021 datosdrive
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/token.h>

void BuildChecksumScript(CScript& ChecksumScript, uint160& ChecksumInput)
{
    ChecksumScript.clear();
    ChecksumScript = CScript() << OP_TOKEN
                                << OP_0
                                << OP_DROP
                                << OP_DUP
                                << OP_HASH160
                                << ToByteVector(ChecksumInput)
                                << OP_EQUALVERIFY
                                << OP_CHECKSIG;
}

bool DecodeChecksumScript(CScript& ChecksumScript, uint160& ChecksumOutput)
{
    if (!ChecksumScript.IsChecksumData()) {
        return false;
    }

    //! retrieve checksum from hash160
    std::vector<unsigned char> vecCksum(ChecksumScript.end() - 22, ChecksumScript.end() - 2);
    memcpy(&ChecksumOutput, vecCksum.data(), 20);

    return true;
}

void BuildTokenScript(CScript& TokenScript, const uint8_t version, const uint16_t type, uint64_t& identifier, std::string& name, CScript& scriptPubKey)
{
    TokenScript.clear();

    // workaround for CScriptNum only accepting 32bit num
    std::vector<uint8_t> vchId(8);
    memcpy(vchId.data(), &identifier, 8);

    TokenScript = CScript() << OP_TOKEN
                             << GetOpcode(version)
                             << GetOpcode(type)
                             << vchId
                             << ToByteVector(name)
                             << OP_DROP
                             << OP_DROP
                             << OP_DROP
                             << OP_DROP;
    TokenScript += scriptPubKey;
}

bool DecodeTokenScript(CScript& TokenScript, uint8_t& version, uint16_t& type, uint64_t& identifier, std::string& name, CPubKey& ownerPubKey, bool debug)
{
    if (!TokenScript.IsPayToToken()) {
        return false;
    }

    int scriptLen = TokenScript.size();

    int byteOffset = 1;

    version = GetIntFromOpcode((opcodetype)TokenScript[byteOffset]);
    if (version != 0x01) {
        LogPrint(BCLog::TOKEN, "%s: bad version\n", __func__);
        return false;
    }
    byteOffset += 1;

    type = GetIntFromOpcode((opcodetype)TokenScript[byteOffset]);
    if (type != 1 && type != 2) {
        LogPrint(BCLog::TOKEN, "%s: bad type\n", __func__);
        return false;
    }
    byteOffset += 1;

    int idLen = TokenScript[byteOffset];
    if (idLen < 1 || idLen > 8) {
        LogPrint(BCLog::TOKEN, "%s: bad idLength\n", __func__);
        return false;
    }
    byteOffset += 1;

    std::vector<unsigned char> vecId(TokenScript.begin() + byteOffset, TokenScript.begin() + byteOffset + idLen);
    // workaround for CScriptNum only accepting 32bit num
    memcpy(&identifier, vecId.data(), idLen);
    byteOffset += idLen;

    int nameLen = TokenScript[byteOffset];
    if (nameLen < TOKENNAME_MINLEN || nameLen > TOKENNAME_MAXLEN) {
        LogPrint(BCLog::TOKEN, "%s: bad nameLen\n", __func__);
        return false;
    }
    byteOffset += 1;

    std::vector<unsigned char> vecName(TokenScript.begin() + byteOffset, TokenScript.begin() + byteOffset + nameLen);
    name = std::string(vecName.begin(), vecName.end());
    byteOffset += nameLen;

    std::vector<unsigned char> vecPubKey(TokenScript.end() - 22, TokenScript.end() - 2);
    std::string hashBytes = HexStr(vecPubKey);

    if (debug) {
        LogPrint(BCLog::TOKEN, "%s (%d bytes) - ver: %d, type %04x, idLen %d, id %016x, nameLen %d, name %s, pubkeyhash %s\n",
            HexStr(TokenScript), scriptLen, version, type, idLen, identifier, nameLen,
            std::string(vecName.begin(), vecName.end()).c_str(), hashBytes);
    }

    return true;
}

bool GetTokenidFromScript(CScript& TokenScript, uint64_t& id, bool debug)
{
    uint8_t version;
    uint16_t type;
    uint64_t identifier;
    std::string name;
    CPubKey ownerKey;
    if (!DecodeTokenScript(TokenScript, version, type, identifier, name, ownerKey, debug)) {
        return false;
    }
    id = identifier;

    return true;
}

bool BuildTokenFromScript(CScript& TokenScript, CToken& token, bool debug)
{
    uint8_t version;
    uint16_t type;
    uint64_t identifier;
    std::string name;
    CPubKey ownerKey;
    if (!DecodeTokenScript(TokenScript, version, type, identifier, name, ownerKey, debug)) {
        return false;
    }

    token.setVersion(version);
    token.setType(type);
    token.setId(identifier);
    token.setName(name);

    return true;
}
