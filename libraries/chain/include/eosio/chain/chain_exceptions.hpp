#pragma once

#include <eosio/chain/types.hpp>

#include <string>
#include <vector>

namespace eosio { namespace chain {

/**
 * A single window of block numbers in which `action_mroot` is allowed to be
 * the zero digest. Used to recover full-history replay past historical
 * incidents where a buggy producer build emitted blocks with zero action_mroot
 * that are now permanently part of the irreversible chain.
 */
struct historical_action_mroot_window {
   uint32_t    from_block = 0;
   uint32_t    to_block   = 0;
   std::string reason;
};

/**
 * Per-chain set of historical validation exceptions. Loaded from an external
 * JSON file at startup; the file must declare the `chain_id` of the chain it
 * targets, and the controller refuses to load it for any other chain. Absent
 * file => strict upstream validation, no bypass.
 */
struct chain_historical_exceptions {
   chain_id_type                                chain_id;
   std::vector<historical_action_mroot_window>  action_mroot_zero_windows;
};

} } // namespace eosio::chain

FC_REFLECT( eosio::chain::historical_action_mroot_window,
            (from_block)(to_block)(reason) )

FC_REFLECT( eosio::chain::chain_historical_exceptions,
            (chain_id)(action_mroot_zero_windows) )
