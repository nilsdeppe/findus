// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Message.hpp"

#include <cstdint>

#include "Rts/DistributedTaskDriver.hpp"

namespace rts {
bool Message_t::execute(
    rts::ThreadPool<Message_t, rts::DistributedTaskDriver*>& /*pool*/,
    const std::uint32_t thread_id, Message_t& message,
    DistributedTaskDriver* distributed_task_driver) {
  distributed_task_driver->invoke(message, thread_id);
  return true;
}
}  // namespace rts
