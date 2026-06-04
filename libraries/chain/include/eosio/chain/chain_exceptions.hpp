#pragma once

#include <eosio/chain/types.hpp>

#include <string>
#include <vector>

namespace eosio { namespace chain {

/**
 * A single window of block numbers in which `action_mroot` is allowed to
 * diverge from the locally-computed value. Used to recover full-history
 * replay past historical incidents where a buggy producer build emitted
 * blocks whose action_mroot is permanently part of the irreversible chain
 * yet cannot be reproduced by a sound replay.
 */
struct historical_action_mroot_window {
   uint32_t    from_block = 0;
   uint32_t    to_block   = 0;
   std::string reason;
};

/**
 * A window in which the implicit `onblock` transaction must be skipped during
 * apply_block. Use when the producer build of record failed to record onblock
 * receipts: re-executing onblock during replay would mutate state (e.g.
 * block_summary_object) inconsistently with the canonical chain and cause
 * permanent divergence from this window onward.
 */
struct historical_onblock_skip_window {
   uint32_t    from_block = 0;
   uint32_t    to_block   = 0;
   std::string reason;
};

/**
 * Suppress the on_activation handler for a specific protocol feature digest.
 * Use when the producer build of record marked a feature as activated but
 * failed to run its activation handler — re-running the handler during replay
 * mutates chainbase (e.g. whitelisted_intrinsics in protocol_state_object)
 * differently from the canonical chain.
 */
struct historical_suppressed_activation {
   digest_type feature_digest;
   std::string reason;
};

/**
 * Per-chain set of historical validation exceptions. May be loaded from an
 * external JSON file at startup, OR matched from a compiled-in registry
 * keyed by `chain_id`. The controller refuses to load a file whose
 * `chain_id` does not match the running chain. Absent file AND no builtin
 * match => strict upstream validation, no bypass.
 *
 * Fields beyond chain_id are optional in the JSON form — older files
 * declaring only action_mroot_zero_windows continue to parse cleanly into
 * a record whose new vectors are empty.
 */
struct chain_historical_exceptions {
   // chain_id_type's default ctor is private upstream; provide an explicit
   // default ctor that uses its public empty_chain_id() factory so this
   // struct is default-constructible (required by fc::variant::as<T>()).
   chain_historical_exceptions()
      : chain_id( chain_id_type::empty_chain_id() ) {}

   chain_id_type                                       chain_id;
   std::vector<historical_action_mroot_window>         action_mroot_zero_windows;
   std::vector<historical_onblock_skip_window>         onblock_skip_windows;
   std::vector<historical_suppressed_activation>       suppressed_activations;
};

/**
 * Compiled-in registry of historical exceptions, keyed by chain_id. Empty
 * unless `coopos` ships with known mainnet incidents baked in. The loader
 * consults this list when no external file is configured: any entry whose
 * chain_id equals the running controller's chain_id is applied verbatim,
 * so archival nodes on known chains work out of the box without an extra
 * config artifact. Forks/testnets with a different chain_id see no entry
 * and behave like upstream Antelope.
 */
const std::vector<chain_historical_exceptions>& get_builtin_historical_exceptions();

} } // namespace eosio::chain

FC_REFLECT( eosio::chain::historical_action_mroot_window,
            (from_block)(to_block)(reason) )

FC_REFLECT( eosio::chain::historical_onblock_skip_window,
            (from_block)(to_block)(reason) )

FC_REFLECT( eosio::chain::historical_suppressed_activation,
            (feature_digest)(reason) )

FC_REFLECT( eosio::chain::chain_historical_exceptions,
            (chain_id)
            (action_mroot_zero_windows)
            (onblock_skip_windows)
            (suppressed_activations) )
