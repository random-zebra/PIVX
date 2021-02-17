// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include "test/test_pivx.h"

#include "base58.h"
#include "key.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(script_P2CS_tests, BasicTestingSetup)

void CheckValidKeyId(const CTxDestination& dest, const CKeyID& expectedKey)
{
    const CKeyID* keyid = boost::get<CKeyID>(&dest);
    if (keyid) {
        BOOST_CHECK(keyid);
        BOOST_CHECK(*keyid == expectedKey);
    } else {
        BOOST_ERROR("Destination is not a CKeyID");
    }
}

// Goal: check cold staking script keys extraction
BOOST_AUTO_TEST_CASE(extract_cold_staking_destination_keys)
{
    CKey ownerKey;
    ownerKey.MakeNewKey(true);
    CKeyID ownerId = ownerKey.GetPubKey().GetID();
    CKey stakerKey;
    stakerKey.MakeNewKey(true);
    CKeyID stakerId = stakerKey.GetPubKey().GetID();
    CScript script = GetScriptForStakeDelegation(stakerId, ownerId);

    // Check owner
    CTxDestination ownerDest;
    BOOST_CHECK(ExtractDestination(script, ownerDest, false));
    CheckValidKeyId(ownerDest, ownerId);

    // Check staker
    CTxDestination stakerDest;
    BOOST_CHECK(ExtractDestination(script, stakerDest, true));
    CheckValidKeyId(stakerDest, stakerId);

    // Now go with ExtractDestinations.
    txnouttype type;
    int nRequiredRet = -1;
    std::vector<CTxDestination> destVector;
    BOOST_CHECK(ExtractDestinations(script, type, destVector, nRequiredRet));
    BOOST_CHECK(type == TX_COLDSTAKE);
    BOOST_CHECK(nRequiredRet == 2);
    BOOST_CHECK(destVector.size() == 2);
    CheckValidKeyId(destVector[0], stakerId);
    CheckValidKeyId(destVector[1], ownerId);
}

static CScript GetNewP2CS()
{
    CKey stakerKey;
    stakerKey.MakeNewKey(true);
    const CKeyID& stakerID = stakerKey.GetPubKey().GetID();
    CKey ownerKey;
    ownerKey.MakeNewKey(true);
    const CKeyID& ownerID = ownerKey.GetPubKey().GetID();
    return GetScriptForStakeDelegation(stakerID, ownerID);
}

static CMutableTransaction CreateNewColdStakeTx(CScript& scriptP2CS)
{
    scriptP2CS = GetNewP2CS();

    // Create from transaction:
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].nValue = 200 * COIN;
    txFrom.vout[0].scriptPubKey = scriptP2CS;

    // Create coldstake
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(2);
    tx.vin[0].prevout.n = 0;
    tx.vin[0].prevout.hash = txFrom.GetHash();
    tx.vout[0].nValue = 0;
    tx.vout[0].scriptPubKey.clear();
    tx.vout[1].nValue = 101 * COIN;
    tx.vout[1].scriptPubKey = scriptP2CS;

    return tx;
}

static CScript GetNewPayee()
{
    CKey key;
    key.MakeNewKey(true);
    const CKeyID& keyId = key.GetPubKey().GetID();
    return GetScriptForDestination(keyId);
}

static bool CheckP2CSScript(const CMutableTransaction& mtx, const CScript& script)
{
    CTransaction tx(mtx);
    return tx.CheckColdStake(script);
}

BOOST_AUTO_TEST_CASE(coldstake_script)
{
    CScript scriptP2CS;
    CMutableTransaction good_tx = CreateNewColdStakeTx(scriptP2CS);
    CMutableTransaction tx(good_tx);
    BOOST_CHECK(CheckP2CSScript(tx, scriptP2CS));

    // Add another p2cs out
    tx.vout.emplace_back(101 * COIN, scriptP2CS);
    BOOST_CHECK(CheckP2CSScript(tx, scriptP2CS));

    // Add a masternode out
    tx.vout.emplace_back(COIN, GetNewPayee());
    BOOST_CHECK(CheckP2CSScript(tx, scriptP2CS));

    // Add another dummy out
    tx.vout.emplace_back(COIN, GetNewPayee());
    BOOST_CHECK(!CheckP2CSScript(tx, scriptP2CS));

    // Replace with new p2cs
    tx = good_tx;
    do {
        tx.vout[1].scriptPubKey = GetNewP2CS();
    } while (tx.vout[1].scriptPubKey == scriptP2CS);
    BOOST_CHECK(!CheckP2CSScript(tx, scriptP2CS));

    // Replace with single dummy out
    tx = good_tx;
    tx.vout[1] = CTxOut(COIN, GetNewPayee());
    BOOST_CHECK(!CheckP2CSScript(tx, scriptP2CS));
    tx.vout.emplace_back(101 * COIN, scriptP2CS);
    BOOST_CHECK(!CheckP2CSScript(tx, scriptP2CS));
}

BOOST_AUTO_TEST_SUITE_END()
