// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2021 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_LLMQ_INIT_H
#define PIVX_LLMQ_INIT_H

class CDBWrapper;
class CEvoDB;

namespace llmq
{

// If true, we will connect to all new quorums and watch their communication
static const bool DEFAULT_WATCH_QUORUMS = false;

// Init/destroy LLMQ globals
void InitLLMQSystem(CEvoDB& evoDb);
void DestroyLLMQSystem();

// Manage scheduled tasks, threads, listeners etc.
void StartLLMQSystem();
void StopLLMQSystem();

} // namespace llmq

#endif // PIVX_LLMQ_INIT_H
