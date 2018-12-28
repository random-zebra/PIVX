// Copyright (c) 2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ArithmeticCircuit2.h"

using namespace libzerocoin;

ArithmeticCircuit2::ArithmeticCircuit2(const ZerocoinParams* p):
            A(0),
            B(0),
            C(0)
{}

void ArithmeticCircuit2::setPreConstraints(const ZerocoinParams* params,
        std::vector< std::vector<ConstraintsList> >& wA,
        std::vector< std::vector<ConstraintsList> >& wB,
        std::vector< std::vector<ConstraintsList> >& wC,
        CBN_vector& K)
{
    const CBigNum a = params->coinCommitmentGroup.g;
    const CBigNum b = params->coinCommitmentGroup.h;
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const int N2 = 1024;

    /* ---------------------------------- **** CONSTRAINTS **** ----------------------------------
     * -------------------------------------------------------------------------------------------
     * Matrices wA, wB, wC and vector K, specifying constraints that ensure that the circuit
     * is satisfied if and only if Cfinal = a^S b^v.
     *
     *     wA      constraints for the left input wires
     *     wB      constraints for the right input wires
     *     wC      constraints for the output wires
     *     K       constraints vector
     *
     *      ##
     *      ##  Constraints check that:
     *      ##   1) For each layer, alpha_i = sum a_{i,j} 2 ** i and beta_i = sum b_{i,j} 2 ** i
     *      ##   2) For each layer, a_{i,j} = 0 or 1 and b_{i,j} = 0 or 1
     *      ##   3) For each layer gamma_i = a^{alpha_i} * b^{beta_i}
     *      ##   4) For each layer except the last alpha_{i+1} or beta_{i+1} = gamma_i
     *      ##   5) alpha_0 or beta_0 are inside the commitment
     *      ##   6) gamma_final = root of merkle tree
     *      ##
     *
     */

    wA.resize(0);
    wB.resize(0);
    wC.resize(0);
    K = CBN_vector(0);

    ConstraintsList zeroList;

    for(int round=0; round<ZKP_treeLength-1; round++) {
        const int roundSize = round*(4*N2+1);
        // Constraints to ensure A[i] + B[i] = 1 for binary_a and binary_b
        for(int i=0, k=1; i<2*N2; i++, k++) {
            std::vector<ConstraintsList> tempA = std::vector<ConstraintsList>(0);
            std::vector<ConstraintsList> tempB = std::vector<ConstraintsList>(0);
            std::vector<ConstraintsList> tempC = std::vector<ConstraintsList>(0);

            ConstraintsList sparseList;
            sparseList.type = ConstraintsListType::Sparse;
            sparseList.row = {roundSize+k, CBigNum(1)};

            tempA.push_back(sparseList);
            tempA.push_back(zeroList);
            wA.push_back(tempA);

            tempB.push_back(sparseList);
            tempB.push_back(zeroList);
            wB.push_back(tempB);

            tempC.push_back(zeroList);
            tempC.push_back(zeroList);
            wC.push_back(tempC);

            K.push_back(CBigNum(1));
        }

        // Constrains to ensure C[i]= 0
        for(int i=0, k=1; i<2*N2; i++, k++) {
            std::vector<ConstraintsList> tempA = std::vector<ConstraintsList>(0);
            std::vector<ConstraintsList> tempB = std::vector<ConstraintsList>(0);
            std::vector<ConstraintsList> tempC = std::vector<ConstraintsList>(0);

            ConstraintsList sparseList;
            sparseList.type = ConstraintsListType::Sparse;
            sparseList.row = {roundSize+k, CBigNum(1)};

            tempA.push_back(zeroList);
            tempA.push_back(zeroList);
            wA.push_back(tempA);

            tempB.push_back(zeroList);
            tempB.push_back(zeroList);
            wB.push_back(tempB);

            tempC.push_back(sparseList);
            tempC.push_back(zeroList);
            wC.push_back(tempC);

            K.push_back(CBigNum(0));
        }

        // Constraints to ensure sum A[i] 2 ** i = A[0]
        std::vector<ConstraintsList> tempA = std::vector<ConstraintsList>(0);
        std::vector<ConstraintsList> tempB = std::vector<ConstraintsList>(0);
        std::vector<ConstraintsList> tempC = std::vector<ConstraintsList>(0);

        CBN_vector temp(roundSize, CBigNum(0));
        temp.push_back(CBigNum(-1) % q);
        for(int i=0; i<N2; i++) {
            temp.push_back(CBigNum(1) >> i);
        }

    }

    return;
}
