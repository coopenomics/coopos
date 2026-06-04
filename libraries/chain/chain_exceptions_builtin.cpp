#include <eosio/chain/chain_exceptions.hpp>

namespace eosio { namespace chain {

namespace {

/**
 * Compile-time registry of known historical exceptions, keyed by chain_id.
 *
 * Add a new entry here when a fix-binary release of coopos must, on a
 * specific production chain, bypass a permanent inconsistency in the
 * irreversible block log left behind by an earlier broken producer build.
 * Each entry is scoped strictly to its chain_id; forks/testnets/devnets with
 * a different chain_id are unaffected.
 *
 * The intent of an entry is to ship enough information in the binary that
 * any node operator on the named chain can simply install the .deb and run
 * a full-history replay — no extra config files, no out-of-band JSON.
 *
 * To override a builtin entry on a specific node (e.g. during forensic
 * investigation) set `chain-historical-exceptions` in config.ini to a JSON
 * file with the matching chain_id; the file replaces the builtin record
 * entirely for that node.
 */
const std::vector<chain_historical_exceptions> _builtin_registry = [] {
   std::vector<chain_historical_exceptions> v;

   // -----------------------------------------------------------------------
   // Coopenomics mainnet (chain_id 6e37f9ac…26c1b)
   //
   // 2026-05-11 ~10:18 UTC: the BP deployed `coopos v5.2.0-dev-294edf3b8`
   // which had two latent defects relative to v5.2.0 release:
   //   - intrinsic table for `assert_recover_key_account` was inserted in
   //     the middle of the table, shifting the IDs of every later intrinsic
   //     (fixed in 5bb051f9a "переместить в конец таблиц — ABI compat");
   //   - the on_activation handler for ASSERT_RECOVER_KEY_ACCOUNT was not
   //     registered (fixed in 2c23b8108).
   // While that build was producing, the implicit onblock action emitted
   // no receipts (`_action_receipt_digests` stayed empty) for 2395 blocks,
   // finalizing `action_mroot = 0`. The BP restarted onto a fixed v5.2.0
   // build at ~10:43:39 UTC; block 113275717 is the first post-recovery
   // block with a non-zero action_mroot. ASSERT_RECOVER_KEY_ACCOUNT itself
   // was only formally activated much later, at block 113318028 (16:36 UTC).
   //
   // For a sound replay against this chain we need to:
   //   (a) accept zero-mroot blocks in [113273322..113275716] even though
   //       our build would compute a non-zero local mroot, because the
   //       canonical chain stores zero;
   //   (b) skip onblock execution in that same window so we do NOT mutate
   //       chainbase (e.g. block_summary_object) for those 2395 blocks —
   //       the canonical chain skipped those mutations and our local state
   //       must match for downstream blocks to validate.
   // No handler suppression is needed: the activation handler that adds
   // `assert_recover_key_account` to whitelisted_intrinsics runs at block
   // 113318028, by which time the BP was already on the fixed binary, so
   // the canonical chain and a v5.3.1 replay agree from that block onward.
   // -----------------------------------------------------------------------
   {
      chain_historical_exceptions e;
      e.chain_id = chain_id_type(
         std::string("6e37f9ac0f0ea717bfdbf57d1dd5d7f0e2d773227d9659a63bbf86eec0326c1b") );

      e.action_mroot_zero_windows.push_back({
         /* from_block */ 113273322u,
         /* to_block   */ 113275716u,
         /* reason     */
         "Coopenomics mainnet: 2395 irreversible blocks finalized with empty "
         "_action_receipt_digests (action_mroot = 0) by v5.2.0-dev-294edf3b8 "
         "between 2026-05-11 10:18 and 10:43 UTC; window boundaries verified "
         "via leap-util block-log print-log on archived blocks.log."
      });

      e.onblock_skip_windows.push_back({
         /* from_block */ 113273322u,
         /* to_block   */ 113275716u,
         /* reason     */
         "Coopenomics mainnet: onblock implicit action did not record receipts "
         "on the buggy BP build (mis-ordered intrinsic table), so the canonical "
         "chain skipped onblock side-effects for these 2395 blocks. A correct "
         "replay must skip onblock here too, otherwise chainbase mutations "
         "diverge from canonical state and every later block fails to validate."
      });

      v.push_back( std::move(e) );
   }

   return v;
}();

} // anonymous namespace

const std::vector<chain_historical_exceptions>& get_builtin_historical_exceptions() {
   return _builtin_registry;
}

} } // namespace eosio::chain
