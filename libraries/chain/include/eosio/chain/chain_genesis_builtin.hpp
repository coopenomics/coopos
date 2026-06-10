#pragma once

#include <eosio/chain/genesis_state.hpp>

#include <string>
#include <vector>

namespace eosio { namespace chain {

/**
 * A named genesis_state shipped inside the binary. The registry lets any
 * operator reproduce the canonical genesis.json of a known chain from the
 * binary alone (`leap-util genesis print <name>`), so bootstrapping a fresh
 * node requires nothing but the package and a p2p address — no out-of-band
 * genesis file that has to be hosted somewhere and can be lost.
 */
struct builtin_genesis_entry {
   std::string   name;     // short well-known name, e.g. "mainnet"
   std::string   note;     // one-line human description
   genesis_state genesis;
};

const std::vector<builtin_genesis_entry>& get_builtin_genesis_registry();

} } // namespace eosio::chain
