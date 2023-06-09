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

    vector<vector<address_v4>> ipMap;

    bool is_receive_loop_running = false;

  public:

    pinger(boost::asio::io_service& io_service) : socket(io_service, icmp::v4())
    {
      std::random_device rd; // obtain a random number from hardware
      gen = std::mt19937(rd()); // seed the generator
    }

    size_t write_to_loop(const char* input, int file_id, size_t position, size_t length, size_t current_length)
    {
      //std::cout << "Write bytes " << length << " starting at " << position << std::endl;

      for (size_t offset = 0; offset < length; offset += write_op.length)
      {
        std::unique_lock<std::mutex> lk(write_op.lock);

        write_op.prepare(file_id, position + offset, length - offset, input + offset);

        //std::cout << "seq " << write_op.sequenceNumber << " " << current_length << " " << std::ceil((double)current_length / DATA_LENGTH) << std::endl;

        if (write_op.sequenceNumber >= std::ceil((double)current_length / DATA_LENGTH))
        {
          send_to_loop_nodes((ushort)distr(gen), file_id, write_op.sequenceNumber, write_op.buffer, write_op.length);
        }
        else
        {
          write_op.wait_for_pending(lk);
        }

        current_length = std::max(current_length, position + offset + write_op.length);
      }

      return length;
    }

    size_t read_from_loop(char* output, size_t file_id, size_t position, size_t length)
    {
      //std::cout << "Read bytes " << length << " starting at " << position << std::endl;

      for (size_t offset = 0; offset < length; offset += read_op.length)
      {
        std::unique_lock<std::mutex> lk(read_op.lock);

        char* buffer = output + offset;
        read_op.prepare(file_id, position + offset, length - offset, buffer);
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
      distr = std::uniform_int_distribution<>(0, (int)smallestList.size() - 1);
    }

    void start_receive_loop()
    {
      is_receive_loop_running = true;
      while (is_receive_loop_running) receive();
    }

    void stop_receive_loop()
    {
      is_receive_loop_running = false;
    }

  private:

    void send_to_loop_nodes(ushort loop_index, int file_id, ushort sequence_number, const char* data, ushort length)
    {
      expected_replies.push_back({ file_id, loop_index, sequence_number });

      for (size_t i = 0; i < ipMap.size(); i++)
      {
        // Get the address to send to
        address_v4 address = ipMap[i][loop_index];
        endpoint.address(address);

        // Create an ICMP header for an echo request.
        icmp_echo_header echo_request(file_id, loop_index, sequence_number, data, length);

        // Stream header and data to request buffer
        std::ostream os(&request_buffer);
        const char* file_id_chars = (const char*)&file_id;
        os << echo_request;
        os.write(file_id_chars, sizeof(int));
        os.write(data, length);
        
        // Send the request.
        //std::cout << "SENDING FOR REAL\n";
        size_t num_bytes_sent = socket.send_to(request_buffer.data(), endpoint);

        // Clear the request buffer so it is ready to be used again
        request_buffer.consume(num_bytes_sent);
      }
    }

    void receive()
    {
      //std::cout << "Wait to Receive" << std::endl;
      // Discard any data already in the buffer.
      reply_buffer.consume(reply_buffer.size());
      ushort length = (ushort)socket.receive(reply_buffer.prepare(DATA_LENGTH * 2));
      //std::cout << "Receive" << std::endl;
      // The actual number of bytes received is committed to the buffer so that we can extract it using a std::istream object.
      reply_buffer.commit(length);

      // Decode the reply packet.
      std::istream is(&reply_buffer);
      ipv4_header ipv4_hdr;
      icmp_header icmp_hdr;
      is >> ipv4_hdr >> icmp_hdr;
      char file_id_chars[sizeof(int)];
      is.read(file_id_chars, sizeof(int));
      int file_id = *(int*)file_id_chars;
      ushort dataLength = (ushort)(length - ipv4_hdr.header_length() - 8 - sizeof(int));
      is.read(receivedData, dataLength);

      if (is && icmp_hdr.type() == icmp_header::echo_reply)
      {
        ushort sequence_number = icmp_hdr.sequence_number();
        ushort id = icmp_hdr.identifier();
        //std::cout << "Received: file " << file_id << " seq " << sequence_number << " id " << id << "length " << dataLength << std::endl;
        auto resultIterator = std::find(expected_replies.begin(), expected_replies.end(), expected_reply(file_id, id, sequence_number));
        if (resultIterator != expected_replies.end())
        {
          const char* data = receivedData;
          //std::cout << "Received data " << *(data) << *(data + 1) << *(data + 2) << *(data + 3) << *(data + 4) << *(data + 5) << *(data + 6) << *(data + 7) << *(data + 8) << *(data + 9) << std::endl;
          expected_replies.erase(resultIterator);

          ushort writeLength = do_operation(write_op, file_id, sequence_number, write_op.buffer, receivedData + write_op.sequenceByteIndex);
          do_operation(read_op, file_id, sequence_number, receivedData + read_op.sequenceByteIndex, read_op.buffer);

          send_to_loop_nodes((ushort)distr(gen), file_id, sequence_number, receivedData, (ushort)std::max(dataLength, writeLength));
        }
      }
    }

    ushort do_operation(drive_operation& op, int file_id, int sequence_number, const char* in_buffer, char* out_buffer)
    {
      bool didOperation = false;
      ushort length = 0;
      {
        // Lock operation so we can check if it is pending
        std::lock_guard<std::mutex> sequenceLock(op.lock);
        // Check for pending operation on this sequence
        if (op.isPending && op.file_id == file_id && op.sequenceNumber == sequence_number)
        {
          // Operation requested on this sequence, carry it out. It could be either:
          //  read_operation: A read from the receive buffer and a write to some out buffer
          //  write_operation: A read from some in buffer and a write to the receive buffer (which then gets sent back out again)
          memcpy(out_buffer, in_buffer, op.length);
          // Operation is no longer pending, calling thread will resume when notified, but first we need to unlock
          op.isPending = false;
          // Keep track of the fact that an operation was performed so we know to notify after unlocking
          didOperation = true;
          length = (ushort)(op.sequenceByteIndex + op.length);
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


  boost::asio::io_service io_service;
  pinger p(io_service);
}

#endif