// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <memory>

#include "Rts/MessageHeader.hpp"

namespace rts {
/// \cond
class DistributedTaskDriver;
template <class MessageType, class ProcessLocalDataType>
class ThreadPool;
/// \endcond

/*!
 * \brief The type of the messages sent by the runtime system.
 *
 * The underlying data is essentially just a byte stream, which is stored in a
 * `std::unique_ptr<std::byte[]>`. There is currently no small message
 * optimization.
 */
struct Message_t {
  std::unique_ptr<std::byte[]> message{nullptr};

  /// @{
  /// \brief Returns the message header.
  MessageHeader* get_header() {
    return reinterpret_cast<MessageHeader*>(message.get());
  }
  const MessageHeader* get_header() const {
    return reinterpret_cast<MessageHeader*>(message.get());
  }
  /// @}

  /// \brief Executes the message.
  static bool execute(
      rts::ThreadPool<Message_t, rts::DistributedTaskDriver*>& /*pool*/,
      const std::uint32_t thread_id, Message_t& message,
      DistributedTaskDriver* distributed_task_driver);
};

/// \brief Make a copy of Message_t.
Message_t copy(const Message_t& message);
}  // namespace rts
