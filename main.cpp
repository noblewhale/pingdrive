#define FUSE_USE_VERSION 31

#include <fuse.h>

#include "pinger.hpp"
#include "pingdrive.hpp"

#include <iostream>
#include <fstream>
#include <thread>

int main(int argc, char* argv[])
{
  boost::asio::io_service io_service;
  pingloop::p = new pingloop::pinger(io_service);

  for (int i = 1; i <= 5; i++)
  {
    std::ifstream ipListFile("IPs-" + std::to_string(i) + ".txt");
    pingloop::p->populate_map(ipListFile);
  }

  std::thread run_thread([&] { pingloop::p->start_receive_loop(); });

  pingloop::p->write_to_loop("test test2", 0, 10);

  fuse_main(argc, argv, &pingloop::drive::operations, NULL);

  run_thread.join();

  delete pingloop::p;
}