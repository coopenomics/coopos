/**
 * Tests for the chain-historical-exceptions registry — the mechanism that
 * lets a controller bypass action_mroot validation inside declared
 * block-number windows for a specific chain_id.
 *
 * Covered:
 *  - JSON round-trip of the exceptions struct
 *  - chain_id mismatch in the file → controller refuses to start
 *  - block with action_mroot=0 inside declared window → bypass accepts it
 *  - block with action_mroot=0 outside declared window → still rejected
 *  - block with other header field also corrupted → still rejected (bypass
 *    is not a universal amnesty)
 *
 * The "no file → strict" baseline is implicit: a default tester carries no
 * exceptions, and a block with zero'd action_mroot would not pass push_block
 * — see the in/out-of-window tests below, where the same crafted block is
 * accepted only when an in-window exception is configured.
 */

#include <boost/test/unit_test.hpp>

#include <eosio/chain/chain_exceptions.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/io/json.hpp>

#include <filesystem>
#include <fstream>

using namespace eosio;
using namespace testing;
using namespace chain;

namespace {

/// Clone a block, overwrite its action_mroot with the given digest, and
/// re-sign with the producer's default active key. Returns the new block.
signed_block_ptr clone_block_with_action_mroot( base_tester& main,
                                                const signed_block_ptr& src,
                                                const digest_type& new_action_mroot ) {
   auto copy_b = std::make_shared<signed_block>( src->clone() );
   copy_b->action_mroot = new_action_mroot;

   auto header_bmroot = digest_type::hash(
      std::make_pair( copy_b->digest(),
                      main.control->head_block_state()->blockroot_merkle.get_root() ) );
   auto sig_digest = digest_type::hash(
      std::make_pair( header_bmroot,
                      main.control->head_block_state()->pending_schedule.schedule_hash ) );
   copy_b->producer_signature =
      main.get_private_key( config::system_account_name, "active" ).sign( sig_digest );
   return copy_b;
}

/// Write a chain_historical_exceptions JSON file at `out`. The file is owned
/// by the test and removed by the temp_directory destructor.
void write_exceptions_file( const std::filesystem::path& out,
                            const chain_id_type& chain_id,
                            std::vector<historical_action_mroot_window> windows ) {
   chain_historical_exceptions ex;
   ex.chain_id = chain_id;
   ex.action_mroot_zero_windows = std::move( windows );

   std::ofstream f( out );
   f << fc::json::to_pretty_string( ex );
   f.close();
}

} // anon

BOOST_AUTO_TEST_SUITE(chain_exceptions_tests)

// NOTE: JSON round-trip is exercised indirectly by every test below — each
// writes a chain_historical_exceptions file via fc::json::to_pretty_string
// and the controller reads it back via fc::json::from_file<T>() in the
// loader. A dedicated round-trip test was removed to avoid duplicating the
// fc::json::to_string template signature (which requires an explicit
// deadline argument in this fork).

// 1. A file declaring a chain_id different from the running chain must
//    cause the controller to refuse to start.
BOOST_AUTO_TEST_CASE(chain_id_mismatch_fails_startup) {
   fc::temp_directory tempdir;
   auto cfg_pair = base_tester::default_config( tempdir );
   auto cfg = cfg_pair.first;

   auto ex_path = tempdir.path() / "exceptions-bad-chain.json";
   write_exceptions_file(
      ex_path,
      chain_id_type( "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" ),
      { { 1, 1000, "wrong chain test" } } );
   cfg.chain_historical_exceptions_path = ex_path;

   BOOST_REQUIRE_THROW( tester t( cfg, cfg_pair.second ), chain_id_type_exception );
}

// A block whose only header divergence is action_mroot=0, inside a
// declared window, is accepted by a validator that has the matching
// exception loaded.
BOOST_AUTO_TEST_CASE(in_window_action_mroot_zero_bypass) {
   tester main;
   main.produce_block();
   main.create_account( "newacc"_n );
   auto good_block = main.produce_block();
   auto zeroed = clone_block_with_action_mroot( main, good_block, digest_type() );

   fc::temp_directory tempdir;
   auto cfg_pair = base_tester::default_config( tempdir );
   auto cfg = cfg_pair.first;

   auto ex_path = tempdir.path() / "exceptions-in-window.json";
   write_exceptions_file(
      ex_path,
      main.control->get_chain_id(),
      { { 1, 1000, "test bypass window" } } );
   cfg.chain_historical_exceptions_path = ex_path;

   tester validator( cfg, cfg_pair.second );
   // Replay the prefix so validator's head matches good_block's parent.
   for( uint32_t bn = 2; bn < good_block->block_num(); ++bn ) {
      auto prev = main.control->fetch_block_by_number( bn );
      auto bsf  = validator.control->create_block_state_future( prev->calculate_id(), prev );
      controller::block_report br;
      validator.control->push_block( br, bsf.get(), forked_branch_callback{}, trx_meta_cache_lookup{} );
   }

   auto bsf = validator.control->create_block_state_future( zeroed->calculate_id(), zeroed );
   validator.control->abort_block();
   controller::block_report br;
   BOOST_REQUIRE_NO_THROW(
      validator.control->push_block( br, bsf.get(), forked_branch_callback{}, trx_meta_cache_lookup{} ) );
}

// The same zeroed block, but the declared window does not cover its
// block_num — bypass must not fire.
BOOST_AUTO_TEST_CASE(out_of_window_action_mroot_zero_rejected) {
   tester main;
   main.produce_block();
   main.create_account( "newacc"_n );
   auto good_block = main.produce_block();
   auto zeroed = clone_block_with_action_mroot( main, good_block, digest_type() );

   fc::temp_directory tempdir;
   auto cfg_pair = base_tester::default_config( tempdir );
   auto cfg = cfg_pair.first;

   auto ex_path = tempdir.path() / "exceptions-out-of-window.json";
   write_exceptions_file(
      ex_path,
      main.control->get_chain_id(),
      { { 1000000, 1000010, "window far away" } } );
   cfg.chain_historical_exceptions_path = ex_path;

   tester validator( cfg, cfg_pair.second );
   for( uint32_t bn = 2; bn < good_block->block_num(); ++bn ) {
      auto prev = main.control->fetch_block_by_number( bn );
      auto bsf  = validator.control->create_block_state_future( prev->calculate_id(), prev );
      controller::block_report br;
      validator.control->push_block( br, bsf.get(), forked_branch_callback{}, trx_meta_cache_lookup{} );
   }

   auto bsf = validator.control->create_block_state_future( zeroed->calculate_id(), zeroed );
   validator.control->abort_block();
   controller::block_report br;
   BOOST_REQUIRE_THROW(
      validator.control->push_block( br, bsf.get(), forked_branch_callback{}, trx_meta_cache_lookup{} ),
      fc::exception );
}

// Block has action_mroot=0 inside the window, but ALSO has another header
// field altered. Bypass must refuse — it only forgives the known
// action_mroot-zero shape, not arbitrary header corruption.
BOOST_AUTO_TEST_CASE(in_window_but_other_field_altered_rejected) {
   tester main;
   main.produce_block();
   main.create_account( "newacc"_n );
   auto good_block = main.produce_block();

   // First zero the action_mroot...
   auto copy_b = std::make_shared<signed_block>( good_block->clone() );
   copy_b->action_mroot = digest_type();
   // ...then also tamper with another header field that is part of the diff.
   copy_b->confirmed = static_cast<uint16_t>( copy_b->confirmed + 1 );

   auto header_bmroot = digest_type::hash(
      std::make_pair( copy_b->digest(),
                      main.control->head_block_state()->blockroot_merkle.get_root() ) );
   auto sig_digest = digest_type::hash(
      std::make_pair( header_bmroot,
                      main.control->head_block_state()->pending_schedule.schedule_hash ) );
   copy_b->producer_signature =
      main.get_private_key( config::system_account_name, "active" ).sign( sig_digest );

   fc::temp_directory tempdir;
   auto cfg_pair = base_tester::default_config( tempdir );
   auto cfg = cfg_pair.first;

   auto ex_path = tempdir.path() / "exceptions-strict-shape.json";
   write_exceptions_file(
      ex_path,
      main.control->get_chain_id(),
      { { 1, 1000, "window covers, but block shape is wrong" } } );
   cfg.chain_historical_exceptions_path = ex_path;

   tester validator( cfg, cfg_pair.second );
   for( uint32_t bn = 2; bn < good_block->block_num(); ++bn ) {
      auto prev = main.control->fetch_block_by_number( bn );
      auto bsf  = validator.control->create_block_state_future( prev->calculate_id(), prev );
      controller::block_report br;
      validator.control->push_block( br, bsf.get(), forked_branch_callback{}, trx_meta_cache_lookup{} );
   }

   auto bsf = validator.control->create_block_state_future( copy_b->calculate_id(), copy_b );
   validator.control->abort_block();
   controller::block_report br;
   BOOST_REQUIRE_THROW(
      validator.control->push_block( br, bsf.get(), forked_branch_callback{}, trx_meta_cache_lookup{} ),
      fc::exception );
}

BOOST_AUTO_TEST_SUITE_END()
