// A loader probe: prints what ednet::load says about one file. Corrupted files are
// made by run_differential.py; each must be refused, and the good file must load.
#include <cstdio>

#include "edison_net.h"

int main(int argc, char **argv) {
  if (argc != 5 && argc != 6) {
    std::fprintf(stderr, "usage: net_load_test net params_sha codes_sha n_codes,id,channels,layers,heads,critic[,1] [id_row_sha]\n");
    return 2;
  }
  ednet::Config g{};
  // A seventh value, 1, expects a version 2 file with an identity map and an unknown row.
  int got = std::sscanf(argv[4], "%d,%d,%d,%d,%d,%d,%d", &g.n_codes, &g.id_dim, &g.channels, &g.layers, &g.heads, &g.critic_hidden, &g.unknown_row);
  if (got != 6 && got != 7) return 2;
  ednet::Expect want{argv[2], argv[3], g};
  if (argc == 6) want.id_row_sha256 = argv[5];
  const char *err = "";
  ednet::Net net;
  if (!ednet::load(net, argv[1], want, &err)) {
    std::printf("REFUSED: %s\n", err);
    return 1;
  }
  std::printf("LOADED\n");
  return 0;
}
