// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "core_io.h"
#include "evo/providertx.h"
#include "evo/specialtx.h"
#include "key.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "wallet/wallet.h"

std::string GetHelpString(int nParamNum, std::string strParamName)
{
    static const std::map<std::string, std::string> mapParamHelp = {
        {"fSend",
            "%d. \"fSend\"                  (boolean, required) Whether to relay the tx to the network or return the hex serialization.\n"
        },
        {"collateralAddress",
            "%d. \"collateralAddress\"      (string, required) The PIVX address to send the collateral to.\n"
        },
        {"collateralOutpoint",
            "%d. \"collateralOutpoint\"     (string, required) The collateral outpoint as JSON object.\n"
            "                                 It must be in the form '{\"txid\": \"xxx\", \"vout\": d}'\n"
        },
        {"collateralIndex",
            "%d. collateralIndex            (numeric, required) The collateral transaction output index.\n"
        },
        {"ipAndPort",
            "%d. \"ipAndPort\"              (string, required) IP and port in the form \"IP:PORT\". Must be unique on the network.\n"
            "                                 Can be set to empty string \"\", which will require a ProUpServTx afterwards.\n"
        },
        {"ownerAddress",
            "%d. \"ownerAddress\"           (string, required) The PIVX address to use for payee updates and governance voting.\n"
            "                                 The private key to to this address must be known to update the MN data.\n"
            "                                 The address must be unused and must differ from the collateralAddress\n"
            "                                 If set to an empty string, a new address is created.\n"
        },
        {"operatorAddress_register",
            "%d. \"operatorPubKey\"         (string, required) The PIVX address for the operator.\n"
            "                                 The corresponding private key must be known to the remote masternode.\n"
            "                                 If set to an empty string, the ownerAddress is used instead.\n"
        },
        {"operatorAddress_update",
            "%d. \"operatorPubKey\"         (string, required) The PIVX address for the operator.\n"
            "                                 The corresponding private key must be known to the remote masternode.\n"
            "                                 If set to an empty string, the currently active operator public key is reused.\n"
        },
        {"votingAddress_register",
            "%d. \"votingAddress\"          (string, required) The voting key address.\n"
            "                                 The private key to to this address must be known to cast budget votes.\n"
            "                                 If set to an empty string, the ownerAddress is used instead.\n"
        },
        {"votingAddress_update",
            "%d. \"votingAddress\"          (string, required) The voting key address.\n"
            "                                 The private key to to this address must be known to cast budget votes.\n"
            "                                 If set to an empty string, the currently active voting key address is reused.\n"
        },
        {"payoutAddress_register",
            "%d. \"payoutAddress\"          (string, required) The PIVX address to use for masternode reward payments.\n"
            "                                 If set to an empty string, the collateral address is used instead.\n"
        },
        {"payoutAddress_update",
            "%d. \"payoutAddress\"          (string, required) The PIVX address to use for masternode reward payments.\n"
            "                                 If set to an empty string, the currently active payout address is reused.\n"
        },
        {"operatorReward",
            "%d. \"operatorReward\"         (numeric, optional, default=0) The %% of the reward to be shared with the operator.\n"
            "                                 The value must be an integer between 0 and 100\n"
        },
        {"operatorPayoutAddress",
            "%d. \"operatorPayoutAddress\"  (string, optional) The address used for operator reward payments.\n"
            "                                 Only allowed when the ProRegTx had a non-zero operatorReward value.\n"
        },


    };

    auto it = mapParamHelp.find(strParamName);
    if (it == mapParamHelp.end())
        throw std::runtime_error(strprintf("FIXME: WRONG PARAM NAME %s!", strParamName));

    return strprintf(it->second, nParamNum);
}

static std::string GetProRegJSON()
{
    return  "   {\n"
            "     \"version\": d,               (numeric) ProRegPL version\n"
            "     \"type\": d,                  (numeric) ProRegPL type\n"
            "     \"mode\": d,                  (numeric) ProRegPL mode\n"
            "     \"collateralOutpoint\": d,    (string) collateral outpoint\n"
            "     \"service\": d,               (string) IP:PORT string\n"
            "     \"keyIDOwner\": d,            (string) hash of owner public key, hex string\n"
            "     \"keyIDOperator\": d,         (string) hash of operator public key, hex string\n"
            "     \"keyIDVoting\": d,           (string) hash of owner public key, hex string\n"
            "     \"payoutAddress\": d,         (string) PIVX Address receiving masternode payouts\n"
            "     \"operatorReward\": d,        (numeric) % value (0-100) to be shared with the operator\n"
            "     \"operatorPayoutAddress\": d  (string) Operator's PIVX Address receiving masternode payouts\n"
            "                                    (only shown if the operatorReward is greater than 0)\n"
            "   }\n";
}

static std::string GetProRegTxOutput()
{
    return "\nResult:\n"
           "{\n";
           "  \"payload\":\n" + GetProRegJSON() +
           "  \"txsize\":                       (numeric) transaction size in bytes\n"
           "  \"fee\":                          (numeric) fee paid by this transaction in PIV"
           "  \"txid\":                         (string) (Only if fSend is set to true) transaction id\n"
           "  \"rawtx\":                        (string) (Only if fSend is set to false) hex encoded raw tx\n"
           "}\n";
}

static CKeyID ParsePubKeyIDFromAddress(const std::string& strAddress)
{
    CTxDestination dest = DecodeDestination(strAddress);
    CKeyID keyID;
    if (IsValidDestination(dest)) {
        return *boost::get<CKeyID>(&dest);
    }

    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("destination not valid %s", strAddress));
}

static CKeyID GetNewKeyID(const std::string& strLabel)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    CTxDestination dest;
    PairResult r = pwalletMain->getNewAddress(dest, strLabel, "receive");
    if(!r.result)
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, *r.status);
    return *boost::get<CKeyID>(&dest);
}

template<typename SpecialTxPayload>
static void FundSpecialTx(CMutableTransaction& tx, SpecialTxPayload payload, CAmount& nFeeRet)
{
    // serialize special tx payload into mutable tx
    tx.SetPayload(payload);

    // add dummy txout as FundTransaction requires at least one output
    if (tx.vout.empty()) {
        tx.vout.emplace_back(0, CScript() << OP_RETURN);
    }

    CFeeRate feeRate = CFeeRate(0);
    int nChangePosInOut = 1;
    std::string strFailReason;
    if (!pwalletMain->FundTransaction(tx, nFeeRet, false, feeRate, nChangePosInOut, strFailReason, false, false))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);

    if (tx.vout.size() > 1) {
        // FundTransaction added a change output, so we don't need the dummy txout anymore
        auto it = std::find(tx.vout.begin(), tx.vout.end(), CTxOut(0, CScript() << OP_RETURN));
        if (it != tx.vout.end()) tx.vout.erase(it);
    }
}

UniValue ProTxRegister(const JSONRPCRequest& request, bool fFundCollateral)
{
    // !TODO: remove after enforcement
    if (!g_IsSaplingActive) {
        throw std::runtime_error("Cannot create ProReg txes yet");
    }

    if (!pwalletMain)
        throw std::runtime_error("wallet not initialized");

    // Initialize ProReg Payload
    CProRegPL pl;

    // Read and validate params
    assert(request.params.size() >= 7 && request.params.size() <= 9);
    const bool fSend = request.params[0].get_bool();
    CTxDestination collDest;

    if (!fFundCollateral) {
        // -- 1. collateralOutpoint
        UniValue o = request.params[1].get_obj();
        const uint256& collateralHash = ParseHashO(o, "txid");
        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be a number");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, collateralIndex cannot be negative");
        pl.collateralOutpoint = COutPoint(collateralHash, nOutput);
        const Coin& coin = pcoinsTip->AccessCoin(pl.collateralOutpoint);
        if (coin.IsSpent() || coin.out.nValue != 10000 * COIN)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "collateral not found, or spent, or with value != 10000 PIV");
        if (coin.out.scriptPubKey.IsPayToColdStaking() ||
                !ExtractDestination(coin.out.scriptPubKey, collDest) || !IsValidDestination(collDest))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "invalid collateral destination. must be either P2PKH or P2PK");
    } else {
        // -- 1. collateralAddress (if empty, create new)
        const std::string& strCollAdd = request.params[1].get_str();
        const CKeyID& keyIDColl = strCollAdd.empty() ? GetNewKeyID("MN-Collateral") : ParsePubKeyIDFromAddress(strCollAdd);
        collDest = CTxDestination(keyIDColl);
        pl.collateralOutpoint = COutPoint(UINT256_ZERO, 0);     // internal collateral, first output
    }

    // -- 2. IP and port
    const std::string& strIpPort = request.params[2].get_str();
    if (!strIpPort.empty() && !Lookup(strIpPort.c_str(), pl.addr, Params().GetDefaultPort(), false))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("invalid network address %s", strIpPort));

    // -- 3. Owner key (if empty, create new)
    const std::string& strOwnerAdd = request.params[3].get_str();
    pl.keyIDOwner = strOwnerAdd.empty() ? GetNewKeyID("MN-Owner") : ParsePubKeyIDFromAddress(strOwnerAdd);

    // -- 4. Operator key (if empty, equal to Owner key)
    const std::string& strOperatorAdd = request.params[4].get_str();
    pl.keyIDOperator = strOperatorAdd.empty() ? pl.keyIDOwner : ParsePubKeyIDFromAddress(strOperatorAdd);

    // -- 5. Voting key (if empty, equal to Owner key)
    const std::string& strKeyVoting = request.params[5].get_str();
    pl.keyIDVoting = strKeyVoting.empty() ? pl.keyIDOwner : ParsePubKeyIDFromAddress(strKeyVoting);

    // Check that the collateral key is different from owner/operator/voting
    if (collDest == CTxDestination(pl.keyIDOwner)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "The Owner key cannot be equal to the collateral address key");
    }
    if (collDest == CTxDestination(pl.keyIDOperator)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "The Operator key cannot be equal to the collateral address key");
    }
    if (collDest == CTxDestination(pl.keyIDVoting)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "The Voting key cannot be equal to the collateral address key");
    }

    // -- 6. Payee (if empty, equal to collateral)
    const std::string& strPayee = request.params[6].get_str();
    if (strPayee.empty()) {
        pl.scriptPayout = GetScriptForDestination(collDest);
    } else {
        CTxDestination payoutDest = ParsePubKeyIDFromAddress(strPayee);
        // Check that the payee key is different from owner/operator/voting
        if (payoutDest == CTxDestination(pl.keyIDOwner)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "The Owner key cannot be equal to the payout address key");
        }
        if (payoutDest == CTxDestination(pl.keyIDOperator)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "The Operator key cannot be equal to the payout address key");
        }
        if (payoutDest == CTxDestination(pl.keyIDVoting)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "The Voting key cannot be equal to the payout address key");
        }
        pl.scriptPayout = GetScriptForDestination(payoutDest);
    }

    // -- 7. Operator reward
    int nOperatorRewardIn = request.params.size() > 7 ? request.params[7].get_int() : 0;
    if (nOperatorRewardIn < 0 || nOperatorRewardIn > 100)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf(
                "invalid operatorReward %d. Must be integer between 0 and 100", nOperatorRewardIn));
    pl.nOperatorReward = (uint8_t)(nOperatorRewardIn & 0xFF);

    // -- 8. Operator Payout address (only if reward > 0)
    const std::string& strOperatorPayee = request.params.size() > 8 ? request.params[8].get_str() : "";
    if (pl.nOperatorReward) {
        pl.scriptOperatorPayout = GetScriptForDestination(ParsePubKeyIDFromAddress(strOperatorPayee));
    } else if (!strOperatorPayee.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify operator payout address when the operator reward is 0");
    }

    // Create special transaction with payload

    CMutableTransaction tx;
    tx.nVersion = CTransaction::SAPLING_VERSION;    // !TODO: remove when SAPLING_VERSION is CURRENT_VERSION
    tx.nType = TxType::TRANSACTION_PROVIDER_REGISTER;

    // Add collateral output if needed, or sign proof of ownership for external collateral
    if (fFundCollateral) {
        tx.vout.emplace_back(10000 * COIN, GetScriptForDestination(collDest));
    } else {
        CKey key;
        if (!pwalletMain->GetKey(*boost::get<CKeyID>(&collDest), key)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to find the private key to the referenced collateral");
        }
        if (!pl.SignProofOfOwnership(key)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to sign proof of ownership of the collateral");
        }
        // Double check
        CValidationState state;
        if (!pl.checkSig(*boost::get<CKeyID>(&collDest), state)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Proof of ownership signature does not verify");
        }
    }

    // Add payload and inputs to the transaction
    CAmount nFeeOut;
    FundSpecialTx(tx, pl, nFeeOut);

    // Fetch previous transactions (inputs) and sign:
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const Coin& coin = pcoinsTip->AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        SignatureData sigdata;
        if (ProduceSignature(MutableTransactionSignatureCreator(pwalletMain, &tx, i, coin.out.nValue, SIGHASH_ALL),
                coin.out.scriptPubKey, sigdata, false)) {
            UpdateTransaction(tx, i, sigdata);
        } else {
            throw std::runtime_error("Signature failed");
        }
    }

    // Construct return object
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("payload", pl.ToJSON());
    ret.pushKV("txsize", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));
    ret.pushKV("fee", ValueFromAmount(nFeeOut));
    std::string rawTx = EncodeHexTx(tx);
    if (!fSend) {
        ret.pushKV("rawtx", rawTx);
    } else {
        // send tx to the network and return txid
        JSONRPCRequest req;
        req.params = UniValue(UniValue::VARR);
        req.params.push_back(rawTx);
        ret.pushKV("txid", sendrawtransaction(req));
    }
    return ret;
}

UniValue dmn_register(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 7 || request.params.size() > 9)
        throw std::runtime_error(
            "dmn_register fSend \"collateralOutpoint\" \"ipAndPort\" \"ownerAddress\" \"operatorAddress\" \"votingAddress\" \"payoutAddress\" ( operatorReward \"operatorPayoutAddress\" )\n"
            "\nCreates and signs a ProTx transaction, to register a Deterministic Masternode, with externally referenced collateral.\n"
            "The collateral is specified through \"collateralOutpoint\" and must be an unspent transaction output, spendable by this wallet\n"
            "(in order to sign a proof of ownership). It must also not be used by any other masternode.\n"
            "If fSend=true sends the tx to the network. Otherwise returns the raw transaction hex, to be sent later with sendrawtransaction.\n"

            "\nArguments:\n"
            + GetHelpString(1, "fSend")
            + GetHelpString(2, "collateralOutpoint")
            + GetHelpString(3, "ipAndPort")
            + GetHelpString(4, "ownerAddress")
            + GetHelpString(5, "operatorAddress_register")
            + GetHelpString(6, "votingAddress_register")
            + GetHelpString(7, "payoutAddress_register")
            + GetHelpString(8, "operatorReward")
            + GetHelpString(9, "operatorPayoutAddress")

            + GetProRegTxOutput() +

            "\nExamples:\n" +
            HelpExampleCli("dmn_register", "false '{\"txid\": \"ad5f667b5f264f8ce1549f099e8e7e5bce29eda652de539f8be60dc300be6674\", \"vout\": 0}' \"168.123.2.118:51472\" "
                                           "\"DNbGaN72dUWCHwHtTcFuVpUqjsrkq6RLiv\" \"\" \"DHLSfgU6fdioFmGNbup6EmuXbVMu2Ekddv\" \"\" 22 \"D6chbBNBhUhMpEXApJexU5eWxZ2rhUeDp6\"") +
            HelpExampleCli("dmn_register", "true '{\"txid\": \"ad5f667b5f264f8ce1549f099e8e7e5bce29eda652de539f8be60dc300be6674\", \"vout\": 0}' \"\" \"\" \"\" \"\" \"\" ") +
            HelpExampleRpc("dmn_register", "false '{\"txid\": \"ad5f667b5f264f8ce1549f099e8e7e5bce29eda652de539f8be60dc300be6674\", \"vout\": 0}', \"168.123.2.118:51472\","
                    "\"DNbGaN72dUWCHwHtTcFuVpUqjsrkq6RLiv\", \"\", \"DHLSfgU6fdioFmGNbup6EmuXbVMu2Ekddu\", \"\", 22, \"D6chbBNBhUhMpEXApJexU5eWxZ2rhUeDp5\""));

    return ProTxRegister(request, false);
}

UniValue dmn_fund(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 7 || request.params.size() > 9)
        throw std::runtime_error(
            "dmn_fund fSend \"collateralAddress\" \"ipAndPort\" \"ownerAddress\" \"operatorAddress\" \"votingAddress\" \"payoutAddress\" ( operatorReward \"operatorPayoutAddress\" )\n"
            "\nCreates and signs ProTx transaction, to register a Deterministic Masternode, with internal collateral.\n"
            "The masternode collateral will be the first output of the tx. The recipient is specified via \"collateralAddress\" and it must be\n"
            "a valid PIVX address (or empty string, in which case a new address is created).\n"
            "If fSend=true sends the tx to the network. Otherwise returns the raw transaction hex, to be sent later with sendrawtransaction.\n"

            "\nArguments:\n"
            + GetHelpString(1, "fSend")
            + GetHelpString(2, "collateralAddress")
            + GetHelpString(3, "ipAndPort")
            + GetHelpString(4, "ownerAddress")
            + GetHelpString(5, "operatorAddress_register")
            + GetHelpString(6, "votingAddress_register")
            + GetHelpString(7, "payoutAddress_register")
            + GetHelpString(8, "operatorReward")
            + GetHelpString(9, "operatorPayoutAddress")

            + GetProRegTxOutput() +

            "\nExamples:\n" +
            HelpExampleCli("dmn_fund", "false \"DPBihkPm5rpc3HdBqpvbGmHNCSKZQZU6Ct\" \"168.123.2.118:51472\" \"DNbGaN72dUWCHwHtTcFuVpUqjsrkq6RLiv\" \"\" \"DHLSfgU6fdioFmGNbup6EmuXbVMu2Ekddv\" \"\" 22 \"D6chbBNBhUhMpEXApJexU5eWxZ2rhUeDp6\"") +
            HelpExampleCli("dmn_fund", "true \"\" \"\" \"\" \"\" \"\" \"\"") +
            HelpExampleRpc("dmn_fund", "false \"DPBihkPm5rpc3HdBqpvbGmHNCSKZQZU6Ct\", \"168.123.2.118:51472\", \"DNbGaN72dUWCHwHtTcFuVpUqjsrkq6RLiv\", \"\", \"DHLSfgU6fdioFmGNbup6EmuXbVMu2Ekddv\", \"\", 22, \"D6chbBNBhUhMpEXApJexU5eWxZ2rhUeDp6\""));

    return ProTxRegister(request, true);
}

