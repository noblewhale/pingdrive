#ifndef PINGER_HEADER_HPP
#define PINGER_HEADER_HPP

#include "global.hpp"

#include "drive_operation.hpp"
#include "expected_reply.hpp"
#include "icmp_header.hpp"
#include "ipv4_header.hpp"

#include <random>
#include <mutex>

namespace pingloop
{
  class pinger
  {
    std::mt19937 gen; // the generator
    std::uniform_int_distribution<> distr; // the distribution

    write_operation write_op;
    read_operation read_op;

    icmp::socket socket;
    streambuf reply_buffer, request_buffer;

    vector<expected_reply> expected_replies;
    char receivedData[DATA_LENGTH];
    icmp::endpoint endpoint;

    int maxSequenceNumber = -1;

    size_t size = 0;
    vector<vector<address_v4>> ipMap;

  public:

    pinger(boost::asio::io_service& io_service) : socket(io_service, icmp::v4())
    {
      std::random_device rd; // obtain a random number from hardware
      gen = std::mt19937(rd()); // seed the generator
    }

    size_t write_to_loop(const char* input, size_t position, size_t length)
    {
      std::cout << "Write bytes " << length << " starting at " << position << std::endl;

      for (int offset = 0; offset < length; offset += write_op.length)
      {
        std::unique_lock<std::mutex> lk(write_op.lock);

        write_op.prepare(position + offset, length - offset, input + offset);

        if (write_op.sequenceNumber > maxSequenceNumber)
        {
          maxSequenceNumber = write_op.sequenceNumber;
          send_to_loop_nodes(distr(gen), write_op.sequenceNumber, write_op.buffer, write_op.length);
        }
        else
        {
          write_op.wait_for_pending(lk);
        }
      }

      size = std::max(size, position + length);

      return length;
    }

    int read_from_loop(char* output, size_t position, size_t length)
    {
      std::cout << "Read bytes " << length << " starting at " << position << std::endl;

      for (int offset = 0; offset < length; offset += read_op.length)
      {
        std::unique_lock<std::mutex> lk(read_op.lock);

        char* buffer = output + offset;
        read_op.prepare(position + offset, length - offset, buffer);
        read_op.wait_for_pending(lk);
      }

      return length;
    }

    void populate_map(std::istream& file)
    {
      std::string ipString;
      vector<address_v4> ipList;
      while (file >> ipString) ipList.push_back(ip::make_address_v4(ipString));
      ipMap.push_back(ipList);

      auto smallestList = *std::min_element(ipMap.begin(), ipMap.end(), [](auto a, auto b) { return a.size() < b.size(); });
      distr = std::uniform_int_distribution<>(0, smallestList.size() - 1);
    }

    void start_receive_loop()
    {
      while (true) receive();
    }

    int get_size()
    {
      return this->size;
    }

  private:

    void send_to_loop_nodes(int loop_index, int sequence_number, const char* data, int length)
    {
      expected_replies.push_back({ loop_index, sequence_number });

      for (int i = 0; i < ipMap.size(); i++)
      {
        // Get the address to send to
        address_v4 address = ipMap[i][loop_index];
        endpoint.address(address);

        // Create an ICMP header for an echo request.
        icmp_echo_header echo_request(loop_index, sequence_number, data, length);

        // Stream header and data to request buffer
        std::ostream os(&request_buffer);
        os << echo_request;
        os.write(data, length);
        
        // Send the request.
        size_t num_bytes_sent = socket.send_to(request_buffer.data(), endpoint);

        // Clear the request buffer so it is ready to be used again
        request_buffer.consume(num_bytes_sent);
      }
    }

    void receive()
    {
      // Discard any data already in the buffer.
      reply_buffer.consume(reply_buffer.size());
      size_t length = socket.receive(reply_buffer.prepare(DATA_LENGTH * 2));

      // The actual number of bytes received is committed to the buffer so that we can extract it using a std::istream object.
      reply_buffer.commit(length);

      // Decode the reply packet.
      std::istream is(&reply_buffer);
      ipv4_header ipv4_hdr;
      icmp_header icmp_hdr;
      is >> ipv4_hdr >> icmp_hdr;
      ushort dataLength = length - ipv4_hdr.header_length() - 8;
      is.read(receivedData, dataLength);

      if (is && icmp_hdr.type() == icmp_header::echo_reply)
      {
        int sequence_number = icmp_hdr.sequence_number();
        int id = icmp_hdr.identifier();
        auto resultIterator = std::find(expected_replies.begin(), expected_replies.end(), expected_reply(id, sequence_number));
        if (resultIterator != expected_replies.end())
        {
          //const char* data = receivedData;
          //std::cout << "Received " << *(data) << *(data + 1) << *(data + 2) << *(data + 3) << *(data + 4) << *(data + 5) << *(data + 6) << *(data + 7) << *(data + 8) << *(data + 9) << std::endl;
          expected_replies.erase(resultIterator);

          ushort writeLength = do_operation(write_op, sequence_number, write_op.buffer, receivedData + write_op.sequenceByteIndex);
          do_operation(read_op, sequence_number, receivedData + read_op.sequenceByteIndex, read_op.buffer);

          send_to_loop_nodes(distr(gen), sequence_number, receivedData, std::max(dataLength, writeLength));
        }
      }
    }

    int do_operation(drive_operation& op, int sequence_number, const char* in_buffer, char* out_buffer)
    {
      bool didOperation = false;
      int length = 0;
      {
        // Lock operation so we can check if it is pending
        std::lock_guard<std::mutex> sequenceLock(op.lock);
        // Check for pending operation on this sequence
        if (op.isPending && op.sequenceNumber == sequence_number)
        {
          // Operation requested on this sequence, carry it out. It could be either:
          //  read_operation: A read from the receive buffer and a write to some out buffer
          //  write_operation: A read from some in buffer and a write to the receive buffer (which then gets sent back out again)
          memcpy(out_buffer, in_buffer, op.length);
          // Operation is no longer pending, calling thread will resume when notified, but first we need to unlock
          op.isPending = false;
          // Keep track of the fact that an operation was performed so we know to notify after unlocking
          didOperation = true;
          length = op.sequenceByteIndex + op.length;
        }
      } // Unlock the operation

      // Notify so that the thread that requested the operation can resume
      if (didOperation)
      {
        op.condition.notify_one();
      }

      return length;
    }
  };

  pinger* p;
}

#endif