#define FUSE_USE_VERSION 31

#include <fuse.h>

#include "pinger.hpp"
#include "pingdrive.hpp"

#include <iostream>
#include <fstream>
#include <thread>

int main(int argc, char* argv[])
{
  for (int i = 0; i <= 3; i++)
  {
    std::ifstream ipListFile("IPs-" + std::to_string(i) + ".txt");
    pingloop::p.populate_map(ipListFile);
  }

  // This runs the timers
  boost::asio::io_service::work work(pingloop::io_service);
  std::thread io_thread([&] { pingloop::io_service.run(); });

  // This runs the network receive->send loop.
  std::thread run_thread([&] { pingloop::p.start_receive_loop(); });

  // This runs the virual filesystem, and blocks
  fuse_main(argc, argv, &pingloop::drive::operations, NULL);

  pingloop::p.stop_receive_loop();

  run_thread.join();

  pingloop::drive::clean_up();
}