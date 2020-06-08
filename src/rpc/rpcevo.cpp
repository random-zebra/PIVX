// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "key.h"
#include "evo/specialtx.h"
#include "messagesigner.h"
#include "rpc/server.h"
#include "wallet/wallet.h"



// Allows to specify PIX address or priv key. In case of PIVX address, the priv key is taken from the wallet
static CKey ParsePrivKey(const std::string &strKeyOrAddress, bool allowAddresses = true) {
    CTxDestination dest = DecodeDestination(strKeyOrAddress);
    if (allowAddresses && IsValidDestination(dest)) {
#ifdef ENABLE_WALLET
        const CKeyID* keyID = boost::get<CKeyID>(&dest);
        CKey key;
        if (!keyID || !pwalletMain->GetKey(*keyID, key))
            throw std::runtime_error(strprintf("non-wallet or invalid address %s", strKeyOrAddress));
        return key;
#else//ENABLE_WALLET
        throw std::runtime_error("addresses not supported in no-wallet builds");
#endif//ENABLE_WALLET
    }

    CKey key = DecodeSecret(strKeyOrAddress);
    if (!key.IsValid())
        throw std::runtime_error(strprintf("invalid priv-key/address %s", strKeyOrAddress));
    return key;
}

static CKeyID ParsePubKeyIDFromAddress(const std::string& strAddress)
{
    CTxDestination dest = DecodeDestination(strAddress);
    CKeyID keyID;
    if (IsValidDestination(dest)) {
        const CKeyID* keyID = boost::get<CKeyID>(&dest);
        if (!keyID)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("address not valid: %s", strAddress));
        return *keyID;
    }

    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("destination not valid %s", strAddress));
}

template<typename SpecialTxPayload>
static void FundSpecialTx(CMutableTransaction& tx, SpecialTxPayload payload)
{
    // resize so that fee calculation is correct
    payload.vchSig.resize(65);

    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    tx.vExtraPayload.assign(ds.begin(), ds.end());

    static CTxOut dummyTxOut(0, CScript() << OP_RETURN);
    bool dummyTxOutAdded = false;
    if (tx.vout.empty()) {
        // add dummy txout as FundTransaction requires at least one output
        tx.vout.emplace_back(dummyTxOut);
        dummyTxOutAdded = true;
    }

    CAmount nFee;
    CFeeRate feeRate = CFeeRate(0);
    int nChangePosInOut = -1;
    std::string strFailReason;
    if (!pwalletMain->FundTransaction(tx, nFee, false, feeRate, nChangePosInOut, strFailReason, false, false))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);

    if (dummyTxOutAdded && tx.vout.size() > 1) {
        // FundTransaction added a change output, so we don't need the dummy txout anymore
        // Removing it results in slight overpayment of fees, but we ignore this for now (as it's a very low amount)
        auto it = std::find(tx.vout.begin(), tx.vout.end(), dummyTxOut);
        assert(it != tx.vout.end());
        tx.vout.erase(it);
    }
}

template<typename SpecialTxPayload>
static void SignSpecialTxPayload(const CMutableTransaction& tx, SpecialTxPayload& payload, const CKey& key)
{
    const uint256& inputsHash = CalcTxInputsHash(tx);
    payload.inputsHash = inputsHash;
    payload.vchSig.clear();

    uint256 hash = ::SerializeHash(payload);
    if (!CHashSigner::SignHash(hash, key, payload.vchSig)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "failed to sign special tx");
    }
}

static std::string SignAndSendSpecialTx(const CMutableTransaction& tx)
{
    LOCK(cs_main);
    CValidationState state;
    if (!CheckSpecialTx(tx, NULL, state))
        throw std::runtime_error(FormatStateMessage(state));

    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << tx;

    /*
    JSONRPCRequest signReqeust;
    signReqeust.params.setArray();
    signReqeust.params.push_back(HexStr(ds.begin(), ds.end()));
    UniValue signResult = signrawtransaction(signReqeust);

    JSONRPCRequest sendRequest;
    sendRequest.params.setArray();
    sendRequest.params.push_back(signResult["hex"].get_str());
    return sendrawtransaction(sendRequest).get_str();
    */
    return "";
}

UniValue protx_register(const UniValue& params, bool fHelp)
{
    const CKey key = ParsePrivKey("whabbalubbadubdub",  true);
}
