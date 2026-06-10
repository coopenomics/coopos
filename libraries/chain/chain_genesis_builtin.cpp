#include <eosio/chain/chain_genesis_builtin.hpp>

namespace eosio { namespace chain {

namespace {

/**
 * Canonical genesis states of known Coopenomics chains.
 *
 * The default-constructed genesis_state already carries the production
 * parameters of the Coopenomics networks (initial_timestamp 2024-07-01,
 * production chain_config values baked into the headers), so each entry only
 * needs to override initial_key. The compile-time default EOSIO_ROOT_KEY is
 * intentionally NOT changed: dev forks and local single-producer setups keep
 * the upstream key and its well-known private key.
 *
 * Verify after any edit: `leap-util genesis list` must print
 *   mainnet  6e37f9ac0f0ea717bfdbf57d1dd5d7f0e2d773227d9659a63bbf86eec0326c1b
 *   testnet  f0364a3f9fd913081f1c0b05c6f8f50a59b2ba60bb928cb321ba3a9a36316624
 */
const std::vector<builtin_genesis_entry> _registry = [] {
   std::vector<builtin_genesis_entry> v;

   {
      builtin_genesis_entry e;
      e.name = "mainnet";
      e.note = "Coopenomics mainnet (chain_id 6e37f9ac…26c1b)";
      e.genesis.initial_key = fc::variant(
         std::string("EOS7TjqL5YfQ7tKzzKr3i1Pa1JkTVrcY2BJhMFfyMPajfAiPThjH7") ).as<public_key_type>();
      v.push_back( std::move(e) );
   }

   {
      builtin_genesis_entry e;
      e.name = "testnet";
      e.note = "Coopenomics testnet (chain_id f0364a3f…16624)";
      e.genesis.initial_key = fc::variant(
         std::string("EOS7izAK61zDPbdUTyNxGfC78XxejdjzXTjixETNrhdDxHdP2SKKb") ).as<public_key_type>();
      v.push_back( std::move(e) );
   }

   return v;
}();

} // anonymous namespace

const std::vector<builtin_genesis_entry>& get_builtin_genesis_registry() {
   return _registry;
}

} } // namespace eosio::chain
