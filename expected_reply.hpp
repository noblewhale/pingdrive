#ifndef EXPECTED_REPLY_HEADER_HPP
#define EXPECTED_REPLY_HEADER_HPP
namespace pingloop
{
  struct expected_reply
  {
    int file_id;
    int loop_index;
    int sequence_number;

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