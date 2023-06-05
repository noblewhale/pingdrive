#ifndef DRIVE_OPERATION_HEADER_HPP
#define DRIVE_OPERATION_HEADER_HPP

#include "global.hpp"

#include <iostream>
#include <mutex>
#include <condition_variable>

namespace pingloop
{
  struct drive_operation
  {
    bool isPending = false;
    ushort sequenceNumber = -1;
    ushort sequenceByteIndex = -1;
    std::condition_variable condition;
    std::mutex lock;
    ushort length = 0;

    void prepare(size_t position, size_t length)
    {
      this->sequenceNumber = (ushort)(position / DATA_LENGTH);
      this->sequenceByteIndex = (ushort)(position % DATA_LENGTH);

      ushort endSequenceByteIndex = (ushort)std::min(this->sequenceByteIndex + length, DATA_LENGTH);
      this->length = endSequenceByteIndex - this->sequenceByteIndex;
    }

    void wait_for_pending(std::unique_lock<std::mutex>& lk)
    {
      std::cout << "Pending operation " << this->sequenceNumber << ":" << this->sequenceByteIndex << ":" << this->length << std::endl;
      this->isPending = true;
      // Wait until operation is done
      this->condition.wait(lk, [this] { return !this->isPending; });
    }
  };

  struct read_operation : drive_operation
  {
    char* buffer = nullptr;

    void prepare(size_t position, size_t length, char* buffer)
    {
      drive_operation::prepare(position, length);
      this->buffer = buffer;
    }
  };

  struct write_operation : drive_operation
  {
    const char* buffer = nullptr;

    void prepare(size_t position, size_t length, const char* buffer)
    {
      drive_operation::prepare(position, length);
      this->buffer = buffer;
    }
  };
}
#endif