// Copyright (c) 2021 pacprotocol
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/token.h>

void build_checksum_script(CScript& checksum_script, uint160& checksum_input)
{
    checksum_script.clear();
    checksum_script = CScript() << OP_TOKEN
                                << OP_0
                                << OP_DROP
                                << OP_DUP
                                << OP_HASH160
                                << ToByteVector(checksum_input)
                                << OP_EQUALVERIFY
                                << OP_CHECKSIG;
}

bool decode_checksum_script(CScript& checksum_script, uint160& checksum_output)
{
    if (!checksum_script.IsChecksumData()) {
        return false;
    }

    //! retrieve checksum from hash160
    std::vector<unsigned char> vecCksum(checksum_script.end() - 22, checksum_script.end() - 2);
    memcpy(&checksum_output, vecCksum.data(), 20);

    return true;
}

void build_token_script(CScript& token_script, const uint8_t version, const uint16_t type, uint64_t& identifier, std::string& name, CScript& scriptPubKey)
{
    token_script.clear();

    // workaround for CScriptNum only accepting 32bit num
    std::vector<uint8_t> vchId(8);
    memcpy(vchId.data(), &identifier, 8);

    token_script = CScript() << OP_TOKEN
                             << GetOpcode(version)
                             << GetOpcode(type)
                             << vchId
                             << ToByteVector(name)
                             << OP_DROP
                             << OP_DROP
                             << OP_DROP
                             << OP_DROP;
    token_script += scriptPubKey;
}

bool decode_token_script(CScript& token_script, uint8_t& version, uint16_t& type, uint64_t& identifier, std::string& name, CPubKey& ownerPubKey, bool debug)
{
    if (!token_script.IsPayToToken()) {
        return false;
    }

    int script_len = token_script.size();

    int byteoffset = 1;

    version = GetIntFromOpcode((opcodetype)token_script[byteoffset]);
    if (version != 0x01) {
        LogPrint(BCLog::TOKEN, "%s: bad version\n", __func__);
        return false;
    }
    byteoffset += 1;

    type = GetIntFromOpcode((opcodetype)token_script[byteoffset]);
    if (type != 1 && type != 2) {
        LogPrint(BCLog::TOKEN, "%s: bad type\n", __func__);
        return false;
    }
    byteoffset += 1;

    int idlen = token_script[byteoffset];
    if (idlen < 1 || idlen > 8) {
        LogPrint(BCLog::TOKEN, "%s: bad idlength\n", __func__);
        return false;
    }
    byteoffset += 1;

    std::vector<unsigned char> vecId(token_script.begin() + byteoffset, token_script.begin() + byteoffset + idlen);
    // workaround for CScriptNum only accepting 32bit num
    memcpy(&identifier, vecId.data(), idlen);
    byteoffset += idlen;

    int namelen = token_script[byteoffset];
    if (namelen < TOKENNAME_MINLEN || namelen > TOKENNAME_MAXLEN) {
        LogPrint(BCLog::TOKEN, "%s: bad namelen\n", __func__);
        return false;
    }
    byteoffset += 1;

    std::vector<unsigned char> vecName(token_script.begin() + byteoffset, token_script.begin() + byteoffset + namelen);
    name = std::string(vecName.begin(), vecName.end());
    byteoffset += namelen;

    std::vector<unsigned char> vecPubKey(token_script.end() - 22, token_script.end() - 2);
    std::string hashBytes = HexStr(vecPubKey);

    if (debug) {
        LogPrint(BCLog::TOKEN, "%s (%d bytes) - ver: %d, type %04x, idlen %d, id %016x, namelen %d, name %s, pubkeyhash %s\n",
            HexStr(token_script), script_len, version, type, idlen, identifier, namelen,
            std::string(vecName.begin(), vecName.end()).c_str(), hashBytes);
    }

    return true;
}

bool get_tokenid_from_script(CScript& token_script, uint64_t& id, bool debug)
{
    uint8_t version;
    uint16_t type;
    uint64_t identifier;
    std::string name;
    CPubKey ownerKey;
    if (!decode_token_script(token_script, version, type, identifier, name, ownerKey, debug)) {
        return false;
    }
    id = identifier;

    return true;
}

bool build_token_from_script(CScript& token_script, CToken& token, bool debug)
{
    uint8_t version;
    uint16_t type;
    uint64_t identifier;
    std::string name;
    CPubKey ownerKey;
    if (!decode_token_script(token_script, version, type, identifier, name, ownerKey, debug)) {
        return false;
    }

    token.setVersion(version);
    token.setType(type);
    token.setId(identifier);
    token.setName(name);

    return true;
}
