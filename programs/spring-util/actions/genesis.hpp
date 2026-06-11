#include "subcommand.hpp"

struct sc_genesis_options {
   std::string name;
};

class genesis_actions : public sub_command<sc_genesis_options> {
public:
   genesis_actions() : sub_command() {}
   void setup(CLI::App& app);

   // callbacks
   int cb_print() const;
   int cb_list() const;
};
