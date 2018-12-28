// Copyright (c) 2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "AccumulatorMerkleTree.h"

namespace libzerocoin {

/*
 * AccumulatorMerkleNode Class
 */

// Computes the hash of the node based on children nodes' respective hashes.
// If left_.hash_ = H1 and right_.hash_ = H2 then this.hash_ = H = ((a**H1) * (b**H2)) % q
// For leaves nodes, returns the hash value.
CBigNum AccumulatorMerkleNode::computeHash()
{
    if (left_ && right_) {
        return (a.pow_mod(left_->getHash(),q)).mul_mod(b.pow_mod(right_->getHash(),q),q);
    }

    return hash_;
}

// Builds a "leaf" node with commitment hash
AccumulatorMerkleNode::AccumulatorMerkleNode(const IntegerGroupParams *coinCommitmentGroup, const CBigNum &hash) :
        a(coinCommitmentGroup->g),
        b(coinCommitmentGroup->h),
        q(coinCommitmentGroup->modulus),
        left_(nullptr),
        right_(nullptr),
        hash_(hash)
{
}


// Creates an intermediate node, storing the descendants as well as computing the compound hash.
// Throws an error if left and right nodes have different parameters. Otherwise inherit them.
AccumulatorMerkleNode::AccumulatorMerkleNode(AccumulatorMerkleNode *left, AccumulatorMerkleNode *right) :
        a(left->a),
        b(left->b),
        q(left->q),
        left_(left),
        right_(right)
{
    // check parameters
    if (left->a != right->a || left->b != right->b || left->q != right->q) {
        throw std::runtime_error("Invalid parameters for accumulator merkle tree node");
    }
    hash_ = computeHash();
}


// Recursively validate the subtree rooted in this node
bool AccumulatorMerkleNode::validate()
{
    // If either child is not valid, the entire subtree is invalid too.
    if (left_ && !left_->validate()) {
      return false;
    }
    if (right_ && !right_->validate()) {
      return false;
    }

    return (hash_ == computeHash());
}


/*
 * AccumulatorMerkleTree Class
 */

// Recursive implementation of the build algorithm used in constructor.
AccumulatorMerkleNode* AccumulatorMerkleTree::build(std::vector<AccumulatorMerkleNode*> nodes, size_t len)
{
    if (len == 1) {
        // return leaf node
        return nodes[0];
    }
    if (len == 2) {
        // build and parent node
        AccumulatorMerkleNode* node = new AccumulatorMerkleNode(nodes[0], nodes[1]);
        nodeMap[node->getHash()] = node;
        return node;
    }
    // build subtrees recursively
    size_t half = len % 2 == 0 ? len/2 : len/2+1;
    AccumulatorMerkleNode* leftChild = build(nodes, half);
    AccumulatorMerkleNode* rightChild = build(std::vector<AccumulatorMerkleNode*>(nodes.begin()+half, nodes.end()), len-half);
    // build and return subtrees parent node
    AccumulatorMerkleNode* node = new AccumulatorMerkleNode(leftChild, rightChild);
    nodeMap[node->getHash()] = node;
    return node;
}

AccumulatorMerkleTree::AccumulatorMerkleTree(const ZerocoinParams* p, const CoinDenomination d, CBN_vector leaves) :
        params(p),
        denomination(d)
{
    // construct the AccumulatorMerkleNode objects from CBigNum leaves values
    std::vector<AccumulatorMerkleNode*> nodes(leaves.size());
    for(unsigned int i=0; i<leaves.size(); i++) {
        AccumulatorMerkleNode* leaf = new AccumulatorMerkleNode(&(p->coinCommitmentGroup), leaves[i]);
        nodeMap[leaf->getHash()] = leaf;
        nodes[i] = leaf;
    }
    // build the tree
    rootNode = build(nodes, nodes.size());
}

AccumulatorMerkleTree::~AccumulatorMerkleTree() {
    for(auto const& x : nodeMap) {
        delete x.second;
    }
}

} /* namespace libzerocoin */
