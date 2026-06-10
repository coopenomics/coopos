/**
 * Tests for the built-in genesis registry (leap-util genesis print/list).
 *
 * The registry must reproduce the CANONICAL genesis of each known chain:
 * the chain_id is the hash of the packed genesis_state, so comparing it
 * against the well-known production values proves every hashed field
 * (initial_timestamp, initial_key, all chain_config values) is correct.
 */

#include <boost/test/unit_test.hpp>

#include <eosio/chain/chain_genesis_builtin.hpp>

using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(chain_genesis_tests)

BOOST_AUTO_TEST_CASE(builtin_genesis_chain_ids_are_canonical) {
   const auto& reg = get_builtin_genesis_registry();
   BOOST_REQUIRE_EQUAL( reg.size(), 2u );

   bool mainnet_found = false, testnet_found = false;
   for( const auto& e : reg ) {
      if( e.name == "mainnet" ) {
         mainnet_found = true;
         BOOST_CHECK_EQUAL( e.genesis.compute_chain_id().str(),
            "6e37f9ac0f0ea717bfdbf57d1dd5d7f0e2d773227d9659a63bbf86eec0326c1b" );
      } else if( e.name == "testnet" ) {
         testnet_found = true;
         BOOST_CHECK_EQUAL( e.genesis.compute_chain_id().str(),
            "f0364a3f9fd913081f1c0b05c6f8f50a59b2ba60bb928cb321ba3a9a36316624" );
      }
   }
   BOOST_REQUIRE( mainnet_found );
   BOOST_REQUIRE( testnet_found );
}

// The registry must not disturb the default genesis used by dev forks: a
// default-constructed genesis_state keeps the compile-time EOSIO_ROOT_KEY,
// which is intentionally different from any production key.
BOOST_AUTO_TEST_CASE(default_genesis_unchanged_for_dev_forks) {
   genesis_state def;
   for( const auto& e : get_builtin_genesis_registry() ) {
      BOOST_CHECK( def.compute_chain_id() != e.genesis.compute_chain_id() );
   }
}

BOOST_AUTO_TEST_SUITE_END()
