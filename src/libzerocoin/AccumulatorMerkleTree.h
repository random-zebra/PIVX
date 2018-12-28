// Copyright (c) 2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ACCMERKLETREE_H_
#define ACCMERKLETREE_H_

#include "Coin.h"
#include "zkplib.h"

namespace libzerocoin {

class AccumulatorMerkleNode {
protected:
    // commitment group parameters (from coinCommitmentGroup)
    const CBigNum a;
    const CBigNum b;
    const CBigNum q;
    // child nodes
    AccumulatorMerkleNode* left_;
    AccumulatorMerkleNode* right_;
    //  hash value
    CBigNum hash_;

    // Computes the hash of the node based on children nodes' respective hashes.
    // If left_.hash_ = H1 and right_.hash_ = H2 then this.hash_ = H = ((a**H1) * (b**H2)) % q
    // For leaves nodes, returns the hash value.
    CBigNum computeHash();

public:
    // Builds a "leaf" node with commitment hash
    AccumulatorMerkleNode(const IntegerGroupParams *coinCommitmentGroup, const CBigNum &hash);

    // Creates an intermediate node, storing the descendants as well as computing the compound hash.
    // Throws an error if left and right nodes have different parameters. Otherwise inherit them.
    AccumulatorMerkleNode(AccumulatorMerkleNode *left, AccumulatorMerkleNode *right);

    // Recursively validate the subtree rooted in this node
    bool validate();

    // Getters
    const CBigNum getHash() const { return hash_; }
    bool hasChildren() const { return left_ || right_; }
    const AccumulatorMerkleNode *left() const { return left_; }
    const AccumulatorMerkleNode *right() const { return right_; }
};


class AccumulatorMerkleTree {
private:
    const ZerocoinParams* params;
    const CoinDenomination denomination;
    AccumulatorMerkleNode* rootNode;

    // maps node hash to node
    std::map<CBigNum, AccumulatorMerkleNode*> nodeMap;

    // Recursive implementation of the build algorithm used in constructor.
    AccumulatorMerkleNode* build(std::vector<AccumulatorMerkleNode*> nodes, size_t len);

public:
    // Construct an AccumulatorMerkleTree from a list of commitments values
    AccumulatorMerkleTree(const ZerocoinParams* p, const CoinDenomination d, CBN_vector leaves);

    // Destructor - delete nodes from the heap
    ~AccumulatorMerkleTree();

    // Getters
    CBigNum getRootHash() { return rootNode->getHash(); }
};

} /* namespace libzerocoin */
#endif /* ACCMERKLETREE_H_ */
