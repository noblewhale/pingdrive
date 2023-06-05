#ifndef GLOBAL_HEADER_HPP
#define GLOBAL_HEADER_HPP

#include <cstddef>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <iostream>

namespace pingloop
{
  static const size_t DATA_LENGTH = 1024;
  static const unsigned char EMPTY_BYTES[DATA_LENGTH] = { 0 };

  namespace ip = boost::asio::ip;
  using ip::icmp;
  using ip::address_v4;
  using boost::asio::streambuf;
  using std::vector;
  using std::tuple;
}

#endif