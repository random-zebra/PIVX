#!/usr/bin/env python3
# Copyright (c) 2021 The PIVX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test deterministic masternodes conflicts and reorgs.
- Check that in-mempool reuse of mn unique-properties is invalid
- Check mempool eviction after conflict with newly connected block / reorg
- Check deterministic list consensus after reorg

"""

import time

from test_framework.test_framework import PivxTestFramework
from test_framework.blocktools import (
    create_block,
    create_coinbase,
)

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    create_new_dmn,
    connect_nodes,
    disconnect_nodes,
    sync_blocks,
    sync_mempools,
)

class TiertwoReorgMempoolTest(PivxTestFramework):

    def set_test_params(self):
        # two nodes mining on separate chains
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-nuparams=v5_shield:1", "-nuparams=v6_evo:160"]] * self.num_nodes
        self.extra_args[0].append("-sporkkey=932HEevBSujW2ud7RfB1YF91AFygbBRQj3de3LyaCRqNzKKgWXi")

    def setup_network(self):
        self.setup_nodes()
        self.connect_all()

    def connect_all(self):
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 0)


    def disconnect_all(self):
        self.log.info("Disconnecting nodes...")
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[1], 0)
        self.log.info("Nodes disconnected")

    def register_masternode(self, from_node, dmn, payout_addr):
        dmn.proTx = from_node.protx_register_fund(payout_addr, dmn.ipport, dmn.owner,
                                                  dmn.operator, dmn.voting, dmn.payee)

    def run_test(self):
        self.disable_mocktime()
        nodeA = self.nodes[0]
        nodeB = self.nodes[1]
        free_idx = 1  # unique id for masternodes. first available.

        # Enforce mn payments and reject legacy mns at block 161
        self.activate_spork(0, "SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT")
        assert_equal("success", self.set_spork(0, "SPORK_21_LEGACY_MNS_MAX_HEIGHT", 160))
        time.sleep(1)
        assert_equal([160] * self.num_nodes, [self.get_spork(x, "SPORK_21_LEGACY_MNS_MAX_HEIGHT")
                                              for x in range(self.num_nodes)])

        # Mine 160 blocks (80 each node)
        self.log.info("Mining...")
        for _ in range(8):
            for i in range(2):
                self.nodes[i].generate(10)
                sync_blocks(self.nodes)
        self.assert_equal_for_all(160, "getblockcount")

        # Disconnect the nodes
        self.disconnect_all()

        #
        # -- CHAIN A --
        #
        mnsA = []
        payout_addr = nodeA.getnewaddress()

        # Register 5 masternodes. One per block.
        self.log.info("Registering masternodes on chain A...")
        for _ in range(5):
            dmn = create_new_dmn(free_idx, nodeA, payout_addr, None)
            free_idx += 1
            self.register_masternode(nodeA, dmn, payout_addr)
            mnsA.append(dmn)
            nodeA.generate(1)
        self.check_mn_list_on_node(0, mnsA)
        self.log.info("Masternodes registered on chain A.")

        # Now send a valid proReg tx to the mempool, without mining it
        mempool_dmn1 = create_new_dmn(free_idx, nodeA, payout_addr, None)
        free_idx += 1
        self.register_masternode(nodeA, mempool_dmn1, payout_addr)
        assert mempool_dmn1.proTx in nodeA.getrawmempool()

        # Try sending a proReg tx with same owner
        self.log.info("Testing in-mempool duplicate-owner rejection...")
        dmn_A1 = create_new_dmn(free_idx, nodeA, payout_addr, None)
        free_idx += 1
        dmn_A1.owner = mempool_dmn1.owner
        assert_raises_rpc_error(-26, "protx-dup",
                                self.register_masternode, nodeA, dmn_A1, payout_addr)
        assert dmn_A1.proTx not in nodeA.getrawmempool()

        # Try sending a proReg tx with same operator
        self.log.info("Testing in-mempool duplicate-operator rejection...")
        dmn_A2 = create_new_dmn(free_idx, nodeA, payout_addr, None)
        free_idx += 1
        dmn_A2.operator = mempool_dmn1.operator
        assert_raises_rpc_error(-26, "protx-dup",
                                self.register_masternode, nodeA, dmn_A2, payout_addr)
        assert dmn_A2.proTx not in nodeA.getrawmempool()

        # Try sending a proReg tx with same IP
        self.log.info("Testing in-mempool duplicate-operator rejection...")
        dmn_A3 = create_new_dmn(free_idx, nodeA, payout_addr, None)
        free_idx += 1
        dmn_A3.ipport = mempool_dmn1.ipport
        assert_raises_rpc_error(-26, "protx-dup",
                                self.register_masternode, nodeA, dmn_A3, payout_addr)
        assert dmn_A3.proTx not in nodeA.getrawmempool()

        # Now send other 2 valid proReg tx to the mempool, without mining it
        mempool_dmn2 = create_new_dmn(free_idx, nodeA, payout_addr, None)
        free_idx += 1
        mempool_dmn3 = create_new_dmn(free_idx, nodeA, payout_addr, None)
        free_idx += 1
        self.register_masternode(nodeA, mempool_dmn2, payout_addr)
        self.register_masternode(nodeA, mempool_dmn3, payout_addr)

        # Now nodeA has 3 proReg txes in its mempool
        mempoolA = nodeA.getrawmempool()
        assert mempool_dmn1.proTx in mempoolA
        assert mempool_dmn2.proTx in mempoolA
        assert mempool_dmn3.proTx in mempoolA

        assert_equal(nodeA.getblockcount(), 165)

        #
        # -- CHAIN B --
        #
        mnsB = []
        payout_addr = nodeB.getnewaddress()
        self.log.info("Registering masternodes on chain B...")

        # Register first the 3 nodes that conflict with the mempool of nodes[0]
        # mine one block after each registration
        for dmn in [dmn_A1, dmn_A2, dmn_A3]:
            self.register_masternode(nodeB, dmn, payout_addr)
            mnsB.append(dmn)
            nodeB.generate(1)
        self.check_mn_list_on_node(1, mnsB)

        # Register 5 more masternodes. One per block.
        for _ in range(5):
            dmn = create_new_dmn(free_idx, nodeB, payout_addr, None)
            free_idx += 1
            self.register_masternode(nodeB, dmn, payout_addr)
            mnsB.append(dmn)
            nodeB.generate(1)

        # Then mine 10 more blocks on chain B
        nodeB.generate(10)
        self.check_mn_list_on_node(1, mnsB)
        self.log.info("Masternodes registered on chain B.")

        assert_equal(nodeB.getblockcount(), 178)

        #
        # -- RECONNECT --
        #

        # Reconnect and sync (give it some more time)
        self.log.info("Reconnecting nodes...")
        self.connect_all()
        sync_blocks([nodeA, nodeB], wait=3, timeout=180)

        # Both nodes have the same list (mnB)
        self.log.info("Checking masternode list...")
        self.check_mn_list_on_node(0, mnsB)
        self.check_mn_list_on_node(1, mnsB)

        # The first mempool proReg tx has been removed from nodeA's mempool
        # due to conflicts with the masternodes of chain B, now connected.
        self.log.info("Checking mempool...")
        mempoolA = nodeA.getrawmempool()
        assert mempool_dmn1.proTx not in mempoolA
        assert mempool_dmn2.proTx in mempoolA
        assert mempool_dmn3.proTx in mempoolA

        # Mine a block from nodeA so the mempool proReg txes get included
        self.log.info("Mining mempool txes...")
        nodeA.generate(1)
        self.sync_all()
        # Check new mn list
        mnsB.append(mempool_dmn2)
        mnsB.append(mempool_dmn3)
        self.check_mn_list_on_node(0, mnsB)
        self.check_mn_list_on_node(1, mnsB)
        self.log.info("Both nodes have %d registered masternodes." % len(mnsB))

        self.log.info("All good.")


if __name__ == '__main__':
    TiertwoReorgMempoolTest().main()
