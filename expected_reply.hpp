#ifndef EXPECTED_REPLY_HEADER_HPP
#define EXPECTED_REPLY_HEADER_HPP

#include <boost/asio.hpp>

namespace pingloop
{
  struct expected_reply
  {
    int file_id;
    int loop_index;
    int sequence_number;
    bool needs_resend = true;

    std::unordered_map<address_v4, boost::asio::deadline_timer*> sub_replies;

    expected_reply()
    {

    }

    expected_reply(int file_id, int loop_index, int sequence_number) : file_id(file_id), loop_index(loop_index), sequence_number(sequence_number)
    {
    }

    bool operator==(expected_reply item) const
    {
      return file_id == item.file_id && loop_index == item.loop_index && sequence_number == item.sequence_number;
    }

    bool operator==(expected_reply item)
    {
      return file_id == item.file_id && loop_index == item.loop_index && sequence_number == item.sequence_number;
    }
  };
}

#endif