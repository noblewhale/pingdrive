#define FUSE_USE_VERSION 31

#include <fuse.h>

#include "pinger.hpp"
#include "pingdrive.hpp"

#include <iostream>
#include <fstream>
#include <thread>

int main(int argc, char* argv[])
{
  for (int i = 1; i <= 5; i++)
  {
    std::ifstream ipListFile("IPs-" + std::to_string(i) + ".txt");
    pingloop::p.populate_map(ipListFile);
  }

  std::thread run_thread([&] { pingloop::p.start_receive_loop(); });

  fuse_main(argc, argv, &pingloop::drive::operations, NULL);

  pingloop::p.stop_receive_loop();

  run_thread.join();

  pingloop::drive::clean_up();
}