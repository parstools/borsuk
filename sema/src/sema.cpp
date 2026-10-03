#include "DocumentCli.h"
int main(int argc, char **argv) {
  return agsem::cli::run(argc, argv, "sema", agsem::cli::processSema);
}
