#ifndef EXPECTED_REPLY_HEADER_HPP
#define EXPECTED_REPLY_HEADER_HPP
namespace pingloop
{
  struct expected_reply
  {
    int loop_index;
    int sequence_number;

    expected_reply(int loop_index, int sequence_number) : loop_index(loop_index), sequence_number(sequence_number)
    {
    }

    bool operator==(expected_reply item) const
    {
      return loop_index == item.loop_index && sequence_number == item.sequence_number;
    }

    bool operator==(expected_reply item)
    {
      return loop_index == item.loop_index && sequence_number == item.sequence_number;
    }
  };
}

#endif