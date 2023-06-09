#ifndef GLOBAL_HEADER_HPP
#define GLOBAL_HEADER_HPP

#include <cstddef>

// save diagnostic state
#pragma GCC diagnostic push 

// turn off the specific warning. Can also use "-Wall"
#pragma GCC diagnostic ignored "-Wconversion"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/path_traits.hpp>

// turn the warnings back on
#pragma GCC diagnostic pop

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
  using std::string;
  using std::tuple;

  template <typename K, typename V>
  using map = std::unordered_map<K, V>;
}

#endif