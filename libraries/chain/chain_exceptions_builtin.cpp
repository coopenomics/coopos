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
std::vector<chain_historical_exceptions> make_builtin_registry() {
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
   // While that build was producing, the implicit onblock action THREW on
   // every block (its exception is swallowed by start_block) — no receipts,
   // no state mutations, `action_mroot` finalized as zero for 2395 blocks
   // [113273322..113275716]. Recovery came in-band: block 113275717 carries
   // a single `eosio::setcode` that replaced the system contract with a
   // build avoiding the broken intrinsic; onblock was still failing while
   // that very block was produced (its mroot covers only the setcode
   // receipt), and from 113275718 onward onblock works again. The BP binary
   // was NOT restarted between those blocks (timestamps are 0.5s apart).
   // ASSERT_RECOVER_KEY_ACCOUNT itself was only formally activated later,
   // at block 113318028 (16:36 UTC), on an already-fixed binary.
   //
   // For a sound replay against this chain we need, through 113275717
   // INCLUSIVE (verified empirically on a full replay 2026-06-10):
   //   (a) skip onblock execution — the canonical chain never applied
   //       onblock's side effects in these blocks, and a replay that runs
   //       onblock diverges in chainbase (sequence counters etc.) from
   //       113275717 onward;
   //   (b) accept an action_mroot mismatch — zero in the canonical chain
   //       for the 2395 empty blocks; in 113275717 the canonical mroot
   //       covers only the setcode receipt.
   // User transactions inside the window still execute normally (there are
   // none in the 2395 empty blocks; 113275717's setcode is applied as on
   // the canonical chain).
   // No handler suppression is needed: the activation handler that adds
   // `assert_recover_key_account` to whitelisted_intrinsics runs at block
   // 113318028, by which time the BP was already on the fixed binary, so
   // the canonical chain and this replay agree from that block onward.
   // -----------------------------------------------------------------------
   {
      chain_historical_exceptions e;
      e.chain_id = chain_id_type(
         std::string("6e37f9ac0f0ea717bfdbf57d1dd5d7f0e2d773227d9659a63bbf86eec0326c1b") );

      e.action_mroot_zero_windows.push_back({
         /* from_block */ 113273322u,
         /* to_block   */ 113275717u,
         /* reason     */
         "Coopenomics mainnet: 2395 irreversible blocks finalized with empty "
         "_action_receipt_digests (action_mroot = 0) by v5.2.0-dev-294edf3b8 "
         "between 2026-05-11 10:18 and 10:43 UTC, plus block 113275717 whose "
         "canonical mroot covers only the recovery setcode receipt (onblock "
         "was still failing when it was produced)."
      });

      e.onblock_skip_windows.push_back({
         /* from_block */ 113273322u,
         /* to_block   */ 113275717u,
         /* reason     */
         "Coopenomics mainnet: the onblock implicit action threw on every "
         "block of the buggy BP build (mis-ordered intrinsic table) up to and "
         "including 113275717, the block whose setcode replaced the system "
         "contract and revived onblock from 113275718 onward. A correct "
         "replay must skip onblock through 113275717, otherwise chainbase "
         "mutations diverge from canonical state and every later block fails "
         "to validate. Verified empirically on a full replay 2026-06-10."
      });

      v.push_back( std::move(e) );
   }

   return v;
}

} // anonymous namespace

const std::vector<chain_historical_exceptions>& get_builtin_historical_exceptions() {
   // Function-local static (first-use init). A namespace-scope static would
   // run during static initialization, before fc's own statics are ready —
   // the equivalent registry in chain_genesis_builtin.cpp crashed leap-util
   // on startup that way (static init order fiasco). chain_id_type parsing
   // happens to survive global init today, but do not rely on it.
   static const std::vector<chain_historical_exceptions> registry = make_builtin_registry();
   return registry;
}

} } // namespace eosio::chain
