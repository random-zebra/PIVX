// Copyright (c) 2018 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/providertx.h"

#include "base58.h"
#include "messagesigner.h"
#include "streams.h"
#include "validation.h"

bool CheckProRegPL(const CTransaction& tx, CValidationState& state)
{
    // Should be called only with ProReg txes
    assert(tx.nType == TRANSACTION_PROVIDER_REGISTER);

    CProRegPL prpl;
    if (!tx.GetPayload(prpl)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-payload");
    }

    if (!prpl.checkVersion(state) ||     // nVersion > CURRENT_VERSION
            !prpl.checkType(state) ||    // nType != 0
            !prpl.checkMode(state) ||    // nMode != 0
            !prpl.checkKeys(state) ||    // some keyID is null
            !prpl.checkPayee(state) ||   // dest not valid or duplicate, or nOperatorReward > 100
            // It's allowed to set empty IP address, which will put the MN into PoSe-banned state and require a
            // ProUpServTx to be issued later. If it is set, it must be  valid.
            (prpl.addr != CService() && !prpl.checkService(state)) ) {
        return false;
    }

    // Check collateral

    CScript collateralSPK;
    if (!prpl.collateralOutpoint.hash.IsNull()) {
        // ProRegTx references an utxo as collateral
        const Coin& coin = pcoinsTip->AccessCoin(prpl.collateralOutpoint);
        if (coin.IsSpent() || coin.out.nValue != 10000 * COIN) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral");
        }
        collateralSPK = coin.out.scriptPubKey;
    } else {
        // ProRegTx has the collateral in one of its outputs
        if (prpl.collateralOutpoint.n >= tx.vout.size()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-index");
        }
        if (tx.vout[prpl.collateralOutpoint.n].nValue != 10000 * COIN) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral");
        }
        collateralSPK = tx.vout[prpl.collateralOutpoint.n].scriptPubKey;
    }

    // P2CS outputs are not valid collaterals
    CTxDestination collateralTxDest;
    if (collateralSPK.IsPayToColdStaking() || !ExtractDestination(collateralSPK, collateralTxDest)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-dest");
    }
    // Extract key from collateral. This only works for P2PK and P2PKH collaterals and will fail for P2SH.
    // Issuer of this ProRegTx must prove ownership with this key by signing the ProRegTx
    if (!IsValidDestination(collateralTxDest)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-pkh");
    }
    // don't allow reuse of collateral key for other keys (don't allow people to put the collateral key onto an online server)
    // this check applies to internal and external collateral, but internal collaterals are not necessarely a P2PKH
    if (collateralTxDest == CTxDestination(prpl.keyIDOwner) ||
        collateralTxDest == CTxDestination(prpl.keyIDOperator) ||
        collateralTxDest == CTxDestination(prpl.keyIDVoting)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-reuse");
    }

    if (!prpl.collateralOutpoint.hash.IsNull()) {
        // the collateral is not part of this ProRegTx, so we must verify ownership of the corresponding key
        CKeyID keyForPayloadSig = *boost::get<CKeyID>(&collateralTxDest);
        if (!prpl.checkSig(keyForPayloadSig, state)) {
            return false;
        }
    } else {
        // collateral is part of this ProRegTx, so we know the collateral is owned by the issuer
        // thus the payload signature must be empty
        if (!prpl.vchSig.empty()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig-not-empty");
        }
    }

    // !TODO: check for duplicate keys/addresses in deterministicMNManager
    return true;
}

/*
 * Provider-Register transaction PayLoad
 */

std::string CProRegPL::GetSignString() const
{
    return ::SerializeHash(*this).ToString();
}

std::string CProRegPL::ToString() const
{
    CTxDestination dest;
    std::string payee = ExtractDestination(scriptPayout, dest) ? EncodeDestination(dest) : "unknown";
    std::string ret = strprintf("CProRegTx(nVersion=%d, nType=%d, nMode=%d, collateralOutpoint=%d, service=%s, keyIDOwner=%s, keyIDOperator=%s, keyIDVoting=%s, payee=%s, nOperatorReward=%f, opPayee=%s)",
                     nVersion,
                     nType,
                     nMode,
                     collateralOutpoint.ToString(),
                     addr.ToString(),
                     keyIDOwner.ToString(),
                     keyIDOperator.ToString(),
                     keyIDVoting.ToString(),
                     payee,
                     (double)nOperatorReward / 100);

    if (nOperatorReward) {
        CTxDestination dest2;
        std::string opPayee = ExtractDestination(scriptOperatorPayout, dest2) ? EncodeDestination(dest2) : "unknown";
        return ret + strprintf(", opPayee=%s)", opPayee);
    }

    return ret + ")";
}

UniValue CProRegPL::ToJSON() const
{
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", nVersion);
    obj.pushKV("type", nType);
    obj.pushKV("mode", nMode);
    obj.pushKV("collateralOutpoint", collateralOutpoint.ToString());
    obj.pushKV("service", addr.ToString());
    obj.pushKV("keyIDOwner", keyIDOwner.ToString());
    obj.pushKV("keyIDOperator", keyIDOperator.ToString());
    obj.pushKV("keyIDVoting", keyIDVoting.ToString());
    CTxDestination dest;
    std::string payee = ExtractDestination(scriptPayout, dest) ? EncodeDestination(dest) : "unknown";
    obj.pushKV("payoutAddress", payee);
    obj.pushKV("operatorReward", (double)nOperatorReward / 100);
    if (nOperatorReward) {
        CTxDestination dest2;
        std::string opPayee = ExtractDestination(scriptOperatorPayout, dest2) ? EncodeDestination(dest2) : "unknown";
        obj.pushKV("operatorPayoutAddress", payee);
    }
    return obj;
}

bool CProRegPL::checkMode(CValidationState& state) const
{
    if (nMode != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-mode");
    }
    return true;
}

bool CProRegPL::checkKeys(CValidationState& state) const
{
    if (keyIDOwner.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-owner-key-null");
    }
    if (keyIDOperator.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-key-null");
    }
    if (keyIDVoting.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-voting-key-null");
    }
    return true;
}

bool CProRegPL::checkPayee(CValidationState& state) const
{
    if (nOperatorReward > 100) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-reward");
    }
    // we will support P2SH later, but restrict it for now (while in transitioning phase from old MN list to deterministic list)
    if (!scriptPayout.IsPayToPublicKeyHash()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
    }
    CTxDestination payoutDest;
    if (!ExtractDestination(scriptPayout, payoutDest) || !IsValidDestination(payoutDest)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-payee-dest-invalid");
    }
    // don't allow reuse of payout key for other keys (don't allow people to put the payee key onto an online server)
    if (payoutDest == CTxDestination(keyIDOwner) ||
        payoutDest == CTxDestination(keyIDOperator) ||
        payoutDest == CTxDestination(keyIDVoting)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-reuse");
    }
    // if operator reward is >0, operator destination must be valid, otherwise it must be empty
    if (nOperatorReward > 0) {
        CTxDestination payoutOperatorDest;
        if (!ExtractDestination(scriptOperatorPayout, payoutOperatorDest) || !IsValidDestination(payoutOperatorDest)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-payee-dest-invalid");
        }
        // don't allow reuse of payout key for other keys (don't allow people to put the payee key onto an online server)
        if (payoutOperatorDest == CTxDestination(keyIDOwner) ||
            payoutOperatorDest == CTxDestination(keyIDOperator) ||
            payoutOperatorDest == CTxDestination(keyIDVoting)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-payee-reuse");
        }
    } else {
        if (scriptOperatorPayout != CScript())
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-payee-not-empty");
    }

    return true;
}

bool CProRegPL::checkService(CValidationState& state) const
{
    if (!addr.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-ipaddr");
    }
    if (!Params().IsRegTestNet() &&
            (addr.GetPort() != Params().GetDefaultPort() || !addr.IsRoutable())) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-ipaddr");
    }
    // !TODO: add support for non-IPv4 addresses
    if (!addr.IsIPv4())
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-addr");

    return true;
}

bool CProRegPL::checkSig(const CKeyID& keyID, CValidationState& state) const
{
    std::string strError;
    if (!CMessageSigner::VerifyMessage(keyID, vchSig, GetSignString(), strError)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig", false, strError);
    }
    return true;
}

bool CProRegPL::checkType(CValidationState& state) const
{
    if (nType != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }
    return true;
}

bool CProRegPL::checkVersion(CValidationState& state) const
{
    if (nVersion == 0 || nVersion > CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }
    return true;
}

bool CProRegPL::SignProofOfOwnership(const CKey& key)
{
    vchSig.clear();
    return CMessageSigner::SignMessage(GetSignString(), vchSig, key);
}
