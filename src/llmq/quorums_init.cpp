// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2021 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "llmq/quorums_init.h"

#include "llmq/quorums_blockprocessor.h"
#include "llmq/quorums_commitment.h"

namespace llmq
{

void InitLLMQSystem(CEvoDB& evoDb)
{
    quorumBlockProcessor.reset(new CQuorumBlockProcessor(evoDb));
}

void DestroyLLMQSystem()
{
    quorumBlockProcessor.reset();
}

} // namespace llmq
