// Copyright (c) 2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include "Coin.h"
#include "zkplib.h"

using namespace std;

namespace libzerocoin {

class ArithmeticCircuit2 {
public:
    ArithmeticCircuit2(const ZerocoinParams* p);
    CBN_matrix A;               // left input wires
    CBN_matrix B;               // right input wires
    CBN_matrix C;               // output wires

    static void setPreConstraints(const ZerocoinParams* params, std::vector< std::vector<ConstraintsList> >& wA, std::vector< std::vector<ConstraintsList> >& wB, std::vector< std::vector<ConstraintsList> >& wC, CBN_vector& K);

private:
    const ZerocoinParams* params;
    CBigNum serialNumber;       // coin serial number S
    CBigNum randomness;         // coin randomness v
    vector<int> r_bits;               // randomness binary decomposition
    CBigNum y;                        // indeterminate of the polynomial equation
    };

} /* namespace libzerocoin */
