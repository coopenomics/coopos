#include "genesis.hpp"

#include <eosio/chain/chain_genesis_builtin.hpp>

#include <fc/io/json.hpp>
#include <fc/variant_object.hpp>

#include <iostream>

using namespace eosio::chain;

void genesis_actions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("genesis", "Built-in genesis utility");
   sub->require_subcommand();

   auto* print = sub->add_subcommand(
      "print",
      "Print the canonical genesis.json of a known chain to stdout. "
      "Redirect to a file and pass it to nodeos via --genesis-json on first "
      "start with an empty data dir; no externally hosted genesis file is needed.");
   print->add_option("name", opt->name, "Chain name as listed by 'genesis list' (e.g. mainnet, testnet)")->required();
   print->callback([this]() {
      int rc = cb_print();
      if(rc) throw(CLI::RuntimeError(rc));
   });

   sub->add_subcommand("list", "List built-in genesis entries with their chain ids")->callback([this]() {
      int rc = cb_list();
      if(rc) throw(CLI::RuntimeError(rc));
   });
}

int genesis_actions::cb_print() const {
   for(const auto& e : get_builtin_genesis_registry()) {
      if(e.name != opt->name) continue;
      // genesis.json layout as consumed by nodeos --genesis-json; chain id is
      // derived from the hashed fields, so it goes to stderr as a hint only.
      std::cout << fc::json::to_pretty_string(e.genesis) << std::endl;
      std::cerr << "# " << e.note << std::endl;
      std::cerr << "# chain_id: " << e.genesis.compute_chain_id().str() << std::endl;
      return 0;
   }

   std::cerr << "unknown genesis name '" << opt->name << "'; available:" << std::endl;
   for(const auto& e : get_builtin_genesis_registry())
      std::cerr << "  " << e.name << std::endl;
   return -1;
}

int genesis_actions::cb_list() const {
   for(const auto& e : get_builtin_genesis_registry()) {
      std::cout << e.name << "  " << e.genesis.compute_chain_id().str() << "  " << e.note << std::endl;
   }
   return 0;
}
