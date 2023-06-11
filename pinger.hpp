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

  boost::asio::io_service io_service;

  /// <summary>Stores data in ICMP echo requests.</summary>
  /// <remarks>
  ///   This class is designed to be used from three different threads.
  ///   THREAD_NETWORK - Runs the receive -> send loop. Blocks while waiting to receive.
  ///   THREAD_TIMER - Runs the time_out timers. ping_expired may be called from this thread or THREAD_NETWORK
  ///   THREAD_DRIVE - The thread that write_to_loop and read_from_loop are called from. This is the fuse thread which is the main thread.
  /// </remarks>
  class pinger
  {
    std::mt19937 gen; // the generator
    std::uniform_int_distribution<> distr; // the distribution
    std::mutex gen_lock;

    write_operation write_op;
    read_operation read_op;

    icmp::socket socket;
    streambuf reply_buffer, request_buffer;

    /// <summary>This list is used to keep track of which replies we are currently expected</summary>
    /// <remarks>
    /// This serves a number of purposes. Each ping is sent to many servers for redundancy, but we only want to send
    /// out a new set of pings the first time a response is received, the rest of the redundant responses are dropped.
    /// The expected_reply entries keep track of whether or not that first reply has been received yet or not.
    /// They also hold the timeout_timer that is used to detect when a ping times out so that we can remove it from
    /// the ipMap.
    /// </remarks>
    vector<expected_reply> expected_replies;
    std::mutex expected_replies_lock;
    char received_data[DATA_LENGTH];
    icmp::endpoint endpoint;

    std::mutex ip_map_lock;
    vector<vector<address_v4>> ip_map;

    std::mutex is_receive_loop_running_lock;
    bool is_receive_loop_running = false;

  public:

    /// <summary>Create a new pinger and initialize the socket and random number generator</summary>
    /// <remarks>
    ///   Called on THREAD_DRIVE before starting fuse
    /// </remarks>
    /// <param name="io_service"></param>
    pinger(boost::asio::io_service& io_service) : socket(io_service, icmp::v4())
    {
      std::lock_guard lk(gen_lock);
      std::random_device rd; // obtain a random number from hardware
      this->gen = std::mt19937(rd()); // seed the generator
    }

    /// <summary>Write some data to the ping loop</summary>
    /// <remarks>
    ///   Called on THREAD_DRIVE from fuse.
    /// </remarks>
    size_t write_to_loop(const char* input, int file_id, size_t position, size_t length, size_t current_length)
    {
      //std::cout << "Write bytes " << length << " starting at " << position << std::endl;

      for (size_t offset = 0; offset < length; offset += this->write_op.length) 
      {
        std::unique_lock<std::mutex> lk(this->write_op.lock);

        this->write_op.prepare(file_id, position + offset, length - offset, input + offset);

        //std::cout << "seq " << write_op.sequenceNumber << " " << current_length << " " << std::ceil((double)current_length / DATA_LENGTH) << std::endl;

        if (this->write_op.sequenceNumber >= std::ceil((double)current_length / DATA_LENGTH))
        {
          std::lock_guard lk(this->gen_lock);
          this->send_to_loop_nodes((ushort)this->distr(this->gen), file_id, this->write_op.sequenceNumber, this->write_op.buffer, this->write_op.length);
        }
        else
        {
          this->write_op.wait_for_pending(lk);
        }

        current_length = std::max(current_length, position + offset + this->write_op.length);
      }

      return length;
    }

    /// <summary>Read some data to the ping loop</summary>
    /// <remarks>
    ///   Called on THREAD_DRIVE from fuse.
    /// </remarks>
    size_t read_from_loop(char* output, size_t file_id, size_t position, size_t length)
    {
      //std::cout << "Read bytes " << length << " starting at " << position << std::endl;

      for (size_t offset = 0; offset < length; offset += this->read_op.length)
      {
        std::unique_lock<std::mutex> lk(this->read_op.lock);

        char* buffer = output + offset;
        this->read_op.prepare(file_id, position + offset, length - offset, buffer);
        this->read_op.wait_for_pending(lk);
      }

      return length;
    }

    /// <summary>Add a list of IPs to use for the pingloop</summary>
    /// <remarks>
    ///   Call this multiple times with similarly sized lists to add redundancy. 
    ///   Each data packet will be sent to a set of addresses chosen at random from each list, so 3 lists = 3 pings per data packet stored in the loop.
    ///   This means the loop will stay alive as long as at least one of the randomly chosen addresses from one of the lists returns a response.
    ///   With only one list the loop is extremely fragile, so it is highly recommended to add multiple lists.
    ///   Called on THREAD_DRIVE before starting fuse.
    /// </remarks>
    /// <param name="file"></param>
    void populate_map(std::istream& file)
    {
      std::lock_guard map_lk(this->ip_map_lock);
      std::string ip_string;
      vector<address_v4> ip_list;
      while (file >> ip_string) ip_list.push_back(ip::make_address_v4(ip_string));
      this->ip_map.push_back(ip_list);

      auto smallestList = *std::min_element(this->ip_map.begin(), this->ip_map.end(), [](auto a, auto b) { return a.size() < b.size(); });
      std::cout << "Smallest list so far: " << smallestList.size() << std::endl;

      std::lock_guard gen_lk(this->gen_lock);
      this->distr = std::uniform_int_distribution<>(0, (int)smallestList.size() - 1);
    }

    /// <summary>Start receiving and echoing back out</summary>
    /// <remarks>
    ///   THREAD_NETWORK starts here
    /// </remarks>
    void start_receive_loop()
    {
      {
        std::lock_guard lk(this->is_receive_loop_running_lock);
        this->is_receive_loop_running = true;
      }
      while (this->is_receive_loop_running)
      {
        this->receive();
      }
    }

    /// <summary>Stop receiving and clean up.</summary>
    /// <remarks>
    ///   Called from THREAD_DRIVE after fuse shuts down</summary>
    /// </remarks>
    void stop_receive_loop()
    {
      {
        std::lock_guard lk(this->is_receive_loop_running_lock);
        this->is_receive_loop_running = false;
      }
      {
        std::lock_guard lk(this->expected_replies_lock);
        for (auto& expected_reply : this->expected_replies)
        {
          for (auto sub_reply_timer : expected_reply.sub_replies)
          {
            delete sub_reply_timer.second;
          }
          expected_reply.sub_replies.clear();
        }
      }
    }

  private:

    void ping_expired(const boost::system::error_code& e, int file_id, ushort loop_index, ushort sequence_number, address_v4 address)
    {
      if (e.value() == boost::asio::error::operation_aborted)
      {
        // This is called on THREAD_NETWORK
        // The timer was cancelled before expiring because a response was received, this is the good and normal path
      }
      else
      {
        // This is called on THREAD_TIMER
        enum ERROR_CODE { DEAD_LOOP, NO_EXPECTED_REPLY, NO_ADDRESS_IN_SUB_REPLIES, TOO_MANY_ADDRESSESS_IN_SUB_REPLIES };
        try
        {
          // Timer expired, BAD PING
          std::cout << "!!! ping expired " << address << " file " << file_id << " seq " << sequence_number << " id " << loop_index << std::endl;;
          // Look for expired reply, there should be one
          {
            std::lock_guard lk(this->expected_replies_lock);
            auto resultIterator = std::find(this->expected_replies.begin(), this->expected_replies.end(), expected_reply(file_id, loop_index, sequence_number));

            // No expired reply found
            if (resultIterator == this->expected_replies.end()) throw NO_EXPECTED_REPLY;

            { // expired_reply only good in this scope I think because it might be erased at the end
              auto& expired_reply = *resultIterator;

              // Make sure the address exists in the sub_replies list
              int num_matching_addresses = expired_reply.sub_replies.count(address);
              if (num_matching_addresses < 1) throw NO_ADDRESS_IN_SUB_REPLIES;
              if (num_matching_addresses > 1) throw TOO_MANY_ADDRESSESS_IN_SUB_REPLIES;

              // Remove the sub-reply since it has timed out
              delete expired_reply.sub_replies[address];
              expired_reply.sub_replies.erase(address);

              if (expired_reply.sub_replies.size() == 0)
              {
                if (expired_reply.needs_resend) throw DEAD_LOOP;
                // Last sub-reply has been removed, so remove the whole expected_reply, it's done now
                this->expected_replies.erase(resultIterator);
              }
            }
          }
        }
        catch (ERROR_CODE e)
        {
          switch (e)
          {
            case NO_EXPECTED_REPLY: std::cout << "Unexpected reply timeout" << std::endl; break;
            case NO_ADDRESS_IN_SUB_REPLIES: std::cout << "NO_ADDRESS_IN_SUB_REPLIES timeout" << std::endl; break;
            case TOO_MANY_ADDRESSESS_IN_SUB_REPLIES: std::cout << "TOO_MANY_ADDRESSESS_IN_SUB_REPLIES timeout" << std::endl; break;
            case DEAD_LOOP: std::cout << "!!!!!!!!!!!!!!!!! A LOOP HAS DIED. ALERT! DEAD LOOP! ALERT! !!!!!!!!!!!!!" << std::endl;
          }
        }
      }
    }

    /// <summary>Send part of some file to a specific node in the loop</summary>
    /// <remarks>
    ///   This can be called on THREAD_DRIVE via write_to_loop or on THREAD_NETWORK via receive
    ///   The lock at the beginning ensures that both don't happen at once.
    /// </remarks>
    void send_to_loop_nodes(ushort loop_index, int file_id, ushort sequence_number, const char* data, ushort length)
    {
      std::lock_guard lk(this->expected_replies_lock);
      this->expected_replies.resize(this->expected_replies.size() + 1);
      expected_reply& er = this->expected_replies[this->expected_replies.size() - 1];
      er.file_id = file_id;
      er.loop_index = loop_index;
      er.sequence_number = sequence_number;
      for (size_t i = 0; i < ip_map.size(); i++)
      {
        // Get the address to send to
        address_v4 address = ip_map[i][loop_index];
        this->endpoint.address(address);
        //std::cout << "Sending to " << address << " file " << file_id << " seq " << sequence_number << " id " << loop_index << " length " << length << std::endl;
        er.sub_replies[address] = new boost::asio::deadline_timer(pingloop::io_service, boost::posix_time::seconds(1));
        er.sub_replies[address]->async_wait([this, file_id, loop_index, sequence_number, address](auto e) { this->ping_expired(e, file_id, loop_index, sequence_number, address); });

        // Create an ICMP header for an echo request.
        icmp_echo_header echo_request(file_id, loop_index, sequence_number, data, length);

        // Stream header and data to request buffer
        std::ostream os(&this->request_buffer);
        const char* file_id_chars = (const char*)&file_id;
        os << echo_request;
        os.write(file_id_chars, sizeof(int));
        os.write(data, length);
        
        // Send the request.
        //std::cout << "SENDING FOR REAL\n";
        size_t num_bytes_sent = this->socket.send_to(this->request_buffer.data(), endpoint);

        // Clear the request buffer so it is ready to be used again
        this->request_buffer.consume(num_bytes_sent);
      }
    }

    /// <summary>Pings are received here</summary>
    /// <remarks>
    ///   Runs on THREAD_NETWORK
    /// </remarks>
    void receive()
    {
      enum ERROR_CODE { NOT_ECHO_RESPONSE, NO_EXPECTED_REPLY, NO_ADDRESS_IN_SUB_REPLIES, TOO_MANY_ADDRESSESS_IN_SUB_REPLIES };
      try
      {
        //std::cout << "Wait to Receive" << std::endl;
        // Discard any data already in the buffer.
        this->reply_buffer.consume(this->reply_buffer.size());
        ushort length = (ushort)this->socket.receive(this->reply_buffer.prepare(DATA_LENGTH * 2));
        //std::cout << "Receive" << std::endl;
        // The actual number of bytes received is committed to the buffer so that we can extract it using a std::istream object.
        this->reply_buffer.commit(length);

        // Decode the reply packet.
        std::istream is(&this->reply_buffer);
        ipv4_header ipv4_hdr;
        icmp_header icmp_hdr;
        is >> ipv4_hdr >> icmp_hdr;
        char file_id_chars[sizeof(int)];
        is.read(file_id_chars, sizeof(int));
        int file_id = *(int*)file_id_chars;
        ushort dataLength = (ushort)(length - ipv4_hdr.header_length() - 8 - sizeof(int));
        is.read(this->received_data, dataLength);

        // Only interested in echo_reply
        if (icmp_hdr.type() != icmp_header::echo_reply) throw NOT_ECHO_RESPONSE;

        ushort sequence_number = icmp_hdr.sequence_number();
        ushort id = icmp_hdr.identifier();

        //std::cout << "Received from " << ipv4_hdr.source_address() << " file " << file_id << " seq " << sequence_number << " id " << id << " length " << dataLength << std::endl;

        ushort writeLength = pinger::do_operation(this->write_op, file_id, sequence_number, this->write_op.buffer, this->received_data + this->write_op.sequenceByteIndex);
        pinger::do_operation(this->read_op, file_id, sequence_number, this->received_data + this->read_op.sequenceByteIndex, this->read_op.buffer);

        bool needs_resend = false;
        {
          std::lock_guard lk(this->expected_replies_lock);

          // Look for expected reply, there should be one
          auto predicate = [file_id, id, sequence_number](expected_reply r) { return r == expected_reply(file_id, id, sequence_number); };
          auto begin_iter = boost::make_filter_iterator(predicate, std::begin(this->expected_replies), std::end(this->expected_replies));
          auto end_iter = boost::make_filter_iterator(predicate, std::end(this->expected_replies), std::end(this->expected_replies));
          //auto resultIterator = std::find(this->expected_replies.begin(), this->expected_replies.end(), expected_reply(file_id, id, sequence_number));
          bool found_expected_reply = false;
          auto expected_reply_it = begin_iter;
          int num_matching_addresses = 0;
          while (expected_reply_it != end_iter)
          {
            expected_reply& expected_reply = *expected_reply_it;
            // Make sure there is a timeout timer for this source address in the sub_replies list
            num_matching_addresses = expected_reply.sub_replies.count(ipv4_hdr.source_address());
            if (num_matching_addresses == 1)
            {
              // Find the timeout timer for this sub-reply and cancel it
              auto& timeout_timer = expected_reply.sub_replies[ipv4_hdr.source_address()];
              timeout_timer->cancel();

              // Remove the sub-reply since we are no longer expecting it
              expected_reply.sub_replies.erase(ipv4_hdr.source_address());
              delete timeout_timer;

              // Check if this is the first reply recieved for this file_id, sequence_number, and loop_id
              // If it is, we need to echo the data back out. If not, nothing is done with the response
              // other than canceling the timeout timer and removing it from the list of expected replies
              needs_resend = expected_reply.needs_resend;
              expected_reply.needs_resend = false;

              if (expected_reply.sub_replies.size() == 0)
              {
                // Last sub-reply has been removed, so remove the whole expected_reply, it's done now
                this->expected_replies.erase(expected_reply_it.base());
              }
              found_expected_reply = true;
              break;
            }
            ++expected_reply_it;
          }
          // No expected reply found
          if (!found_expected_reply) throw NO_EXPECTED_REPLY;
          if (num_matching_addresses < 1) throw NO_ADDRESS_IN_SUB_REPLIES;
          if (num_matching_addresses > 1) throw TOO_MANY_ADDRESSESS_IN_SUB_REPLIES;
        }

        if (needs_resend)
        {
          std::lock_guard lk(this->gen_lock);
          this->send_to_loop_nodes((ushort)this->distr(this->gen), file_id, sequence_number, this->received_data, (ushort)std::max(dataLength, writeLength));
        }
      }
      catch (ERROR_CODE e)
      {
        switch (e)
        {
          case NO_EXPECTED_REPLY: std::cout << "Unexpected reply received" << std::endl; break;
          case NO_ADDRESS_IN_SUB_REPLIES: std::cout << "NO_ADDRESS_IN_SUB_REPLIES" << std::endl; break;
          case TOO_MANY_ADDRESSESS_IN_SUB_REPLIES: std::cout << "TOO_MANY_ADDRESSESS_IN_SUB_REPLIES" << std::endl; break;
        }
      }
    }

    static ushort do_operation(drive_operation& op, int file_id, int sequence_number, const char* in_buffer, char* out_buffer)
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

  pinger p(io_service);
}

#endif