// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "Rts/Detail/GetOutput.hpp"
#include "Rts/Detail/IndexConversion.hpp"
#include "Rts/Detail/ReductionCounter.hpp"
#include "Rts/DistributedObjectIndex.hpp"
#include "Rts/Exceptions/Exception.hpp"
#include "Rts/HardwareInfo.hpp"
#include "Rts/IsCollection.hpp"
#include "Rts/Message.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/MessageType.hpp"
#include "Rts/ParentAndChildren.hpp"

namespace rts::reduction {
namespace detail {
struct ReductionCallbackImpl {
  std::uint64_t collection_index_{std::numeric_limits<std::uint64_t>::max()};
  std::uint32_t distributed_object_index_{
      std::numeric_limits<std::uint32_t>::max()};
  MessageType message_type_{MessageType::Uninitialized};
};
}  // namespace detail

/*!
 * \brief Callback object invoked after a reduction operation completes.
 *
 * The ReductionCallback class encapsulates the information needed to perform
 * a post-reduction action, such as invoking a specific action on a parallel
 * component or broadcasting the result to all elements of a collection.
 *
 * \tparam Action The action to be invoked after the reduction completes.
 * \tparam ParallelComponent The parallel component on which the action will be
 *         invoked.
 *
 * Usage:
 * - For a broadcast reduction callback, use the default constructor.
 * - For an invoke reduction callback (targeting a specific collection element),
 *   use the constructor that takes a collection index.
 *
 */
template <class Action, class ParallelComponent>
class ReductionCallback : public detail::ReductionCallbackImpl {
 public:
  /// \brief Create a Broadcast reduction callback.
  ReductionCallback();

  /// \brief Create an Invoke reduction callback.
  template <class T = ParallelComponent>
  explicit ReductionCallback(
      rts::detail::get_collection_index<T> collection_index);
};

template <class Action, class ParallelComponent>
template <class T>
ReductionCallback<Action, ParallelComponent>::ReductionCallback(
    const rts::detail::get_collection_index<T> collection_index)
    : detail::ReductionCallbackImpl{
          [](const rts::detail::get_collection_index<T> index) {
            if constexpr (is_collection_v<ParallelComponent>) {
              return rts::detail::to_internal(index);
            } else {
              return static_cast<std::uint64_t>(index);
            }
          }(collection_index),
          rts::detail::distributed_object_index<ParallelComponent>(),
          MessageType::Invoke} {}

template <class Action, class ParallelComponent>
ReductionCallback<Action, ParallelComponent>::ReductionCallback()
    : detail::ReductionCallbackImpl{
          MessageHeader::no_collection_index(),
          rts::detail::distributed_object_index<ParallelComponent>(),
          MessageType::Broadcast} {}

/// \brief Equivalence operator for `ReductionCallback`.
template <class Action, class ParallelComponent>
bool operator==(const ReductionCallback<Action, ParallelComponent>& lhs,
                const ReductionCallback<Action, ParallelComponent>& rhs) {
  return lhs.collection_index_ == rhs.collection_index_ and
         lhs.message_type_ == rhs.message_type_;
}

/// \brief Inequivalence operator for `ReductionCallback`.
template <class Action, class ParallelComponent>
bool operator!=(const ReductionCallback<Action, ParallelComponent>& lhs,
                const ReductionCallback<Action, ParallelComponent>& rhs) {
  return not(lhs == rhs);
}

/*!
 * \brief Indicates the result of attempting to insert or combine reduction
 * data.
 *
 * This enum is used to communicate the outcome of an insert or combine
 * operation in the reduction data handler.
 */
enum class InsertAction {
  /*!
   * \brief A new entry was inserted for the given reduction ID.
   *
   * This value indicates that the reduction data and callback were newly
   * inserted into the handler, as no existing entry for the reduction ID
   * was found.
   */
  Insert,

  /*!
   * \brief The reduction data was combined with an existing entry.
   *
   * This value indicates that an entry for the reduction ID already existed,
   * and the provided data was combined with the existing data using the
   * reduction operation.
   */
  Combine,

  /*!
   * \brief The reduction operation is complete.
   *
   * This value indicates that the reduction has finished and no further
   * insertions or combinations are needed for the given reduction ID.
   */
  Complete,

  /*!
   * \brief The reduction operation could not be performed because there are
   * too many simultaneous reductions.
   */
  AtCapacity
};

/*!
 * \brief Stream insertion operator for InsertAction.
 *
 * Writes a human-readable string representation of the InsertAction enum value
 * to the provided output stream.
 *
 * \param os The output stream.
 * \param action The InsertAction enum value to write.
 * \return The output stream.
 */
std::ostream& operator<<(std::ostream& os, const InsertAction action);

namespace detail {
template <class BinaryOp, class Data_t, size_t... Is>
void combine_impl(Message_t& message0, const Message_t& message1,
                  std::index_sequence<Is...> /*meta*/) {
  const Data_t& message1_data =
      *data_from_message<Data_t>(*message1.get_header());
  Data_t& message0_data = *data_from_message<Data_t>(*message0.get_header());
  BinaryOp{}(message0_data, std::get<Is>(message1_data)...);
}

/*!
 * \brief Combines the reduction data from two messages using a binary
 * operation.
 *
 * This function merges the data from `message1` into `message0` using the
 * specified binary operation (`BinaryOp`). The data in both messages must not
 * have been serialized (i.e., must be in-place constructed). The reduction IDs
 * in both messages must match, otherwise an exception is thrown.
 *
 * \tparam BinaryOp The binary operation to use for combining the data.
 * \tparam Data_t The type of the data tuple stored in the message.
 * \param message0 The message whose data will be updated in-place.
 * \param message1 The message whose data will be combined into message0.
 *
 * \throws Exception if the data in either message was serialized or if the
 *         reduction IDs do not match.
 */
template <class BinaryOp, class Data_t>
void combine(Message_t& message0, const Message_t& message1) {
  if (message0.get_header()->data_was_serialized() or
      message1.get_header()->data_was_serialized()) {
    throw Exception{"Cannot currently combine data that was serialized."};
  }
  const std::uint64_t reduction_id0 = get_id(message0);
  const std::uint64_t reduction_id1 = get_id(message1);
  if (reduction_id0 != reduction_id1) {
    throw Exception{
        "The reduction id in the two reduction messages must match but "
        "message0 has: " +
        std::to_string(reduction_id0) +
        " and message1 has: " + std::to_string(reduction_id1)};
  }
  combine_impl<BinaryOp, Data_t>(
      message0, message1,
      std::make_index_sequence<std::tuple_size_v<Data_t>>{});
}
}  // namespace detail

/*!
 * \brief Handles storage and combining of reduction data for a single thread.
 *
 * The DataHandler class manages reduction data and associated callbacks for
 * reductions performed within a thread. It supports concurrent insertions and
 * combinations of reduction data, using atomic operations to ensure thread
 * safety. Each reduction is identified by a unique reduction ID, and the
 * handler provides efficient lookup, insertion, and combining of reduction
 * messages. The handler is designed for use in a multi-threaded environment
 * where each thread maintains its own DataHandler instance.
 *
 * The class is aligned to the hardware's destructive interference size to
 * minimize false sharing and improve performance in multi-threaded scenarios.
 */
class alignas(rts::hardware_info::hardware_destructive_interference_size)
    DataHandler {
 public:
  /*!
   * \brief Because of the need for thread-safety, there is no useful case
   * where this class is default-constructed.
   */
  DataHandler() = delete;

  /*!
   * \brief Constructs a DataHandler with a fixed number of reduction slots.
   *
   * \param max_simultaneous_reductions The maximum number of reductions that
   *        can be tracked simultaneously by this handler.
   *
   * This constructor pre-allocates storage for the specified number of
   * reductions. Each slot can hold one reduction's data and callback.
   */
  explicit DataHandler(size_t max_simultaneous_reductions);

  /*!
   * \brief Inserts new reduction data or combines with existing data.
   *
   * If the given reduction ID is not present, a new entry is created with the
   * provided data and callback. If the reduction ID already exists, the data
   * is combined with the existing entry using the specified binary operation.
   *
   * \tparam BinaryOp The binary operation used for combining data.
   * \tparam BroadcastAction The action type for the broadcast.
   * \tparam BroadcastParallelComponent The parallel component type.
   * \tparam Args The types of the reduction data arguments.
   * \param message_type The `MessageType`, either `Reduction` or
   *                     `ReductionOver`
   * \param distributed_object_index The index of the distributed object.
   * \param reduction_id The unique reduction ID.
   * \param reduction_callback The callback to invoke after reduction.
   * \param args The reduction data arguments.
   * \return InsertAction indicating whether a new entry was inserted or data
   *         was combined.
   *
   * \throws `rts::Exception` if the reduction ID is zero or if insertion fails.
   */
  template <class BinaryOp, class BroadcastAction,
            class BroadcastParallelComponent, class... Args>
  InsertAction insert_or_combine(
      MessageType message_type, std::uint32_t distributed_object_index,
      std::uint64_t reduction_id,
      ReductionCallback<BroadcastAction, BroadcastParallelComponent>
          reduction_callback,
      Args&&... args);

  /*!
   * \brief Removes and returns the reduction message for a given reduction ID.
   *
   * This function locates the entry for the specified reduction ID, removes it
   * from the handler, and returns the associated message.
   *
   * \param reduction_id The unique reduction ID.
   * \return The message associated with the reduction ID.
   *
   * \throws `rts::Exception` if the reduction ID is not found.
   */
  Message_t pop(std::uint64_t reduction_id);

  /*!
   * \brief Finds the index of a reduction entry by its reduction ID.
   *
   * \param reduction_id The unique reduction ID to search for.
   * \return The index of the entry if found, or std::nullopt if not found.
   */
  std::optional<std::uint64_t> index_of(std::uint64_t reduction_id) const;

  /*!
   * \brief Returns the maximum number of simultaneous reductions that can be
   * done over a single parallel component.
   */
  size_t capacity() const;

 private:
  /*!
   * \brief Holds the reduction ID and associated data for a single reduction.
   *
   * Each entry in the DataHandler consists of a reduction ID, the message
   * containing the reduction data and callback, and padding to avoid false
   * sharing. The reduction ID is stored atomically to support concurrent
   * access.
   *
   * The class is aligned to the hardware's destructive interference size to
   * minimize false sharing and improve performance in multi-threaded scenarios.
   */
  struct alignas(rts::hardware_info::hardware_destructive_interference_size)
      ReductionIdAndData {
    std::atomic<std::uint64_t> reduction_id{0};
    Message_t callback_and_data{};
    std::array<std::byte,
               rts::hardware_info::hardware_destructive_interference_size %
                   (sizeof(std::atomic<std::uint64_t>) + sizeof(Message_t))>
        cacheline_fill{};
  };

  alignas(rts::hardware_info::hardware_destructive_interference_size)
      std::vector<ReductionIdAndData> entries_{};
};

template <class BinaryOp, class BroadcastAction,
          class BroadcastParallelComponent, class... Args>
InsertAction DataHandler::insert_or_combine(
    const MessageType message_type,
    const std::uint32_t distributed_object_index,
    const std::uint64_t reduction_id,
    ReductionCallback<BroadcastAction, BroadcastParallelComponent>
        reduction_callback,
    Args&&... args) {
  if (reduction_id == 0) {
    throw Exception{
        "The key value of 0 is not supported in reductions because it is "
        "used as a sentinel."};
  }
  if (message_type != MessageType::Reduction and
      message_type != MessageType::ReductionOver) {
    throw Exception{
        "MessageType passed to DataHandler::insert_or_combine must be "
        "Reduction or ReductionOver but got " +
        rts::detail::get_output(message_type)};
  }
  using Data_t = std::tuple<std::decay_t<Args>...>;

  // We only need atomics to allow the thread doing the reduction across all
  // threads on the process to be able to safely check and remove a slot. We
  // are guaranteed that the only contention is that a hash collision may
  // cause a slot to be read and used by the thread doing the internode
  // reduction while a thread is adding a new reduction locally. This means
  // we only have a single writer, and so we only have an acquire barrier in
  // the fetch_add on the reduction_id.
  for (std::uint64_t index = reduction_id, counter = 0;
       counter < entries_.size();
       (void)++index, (void)++counter) {  // loop for linear probing
    index = index bitand (entries_.size() - 1);
    const std::uint64_t probed_reduction_id = entries_[index].reduction_id.load(
        std::memory_order::memory_order_relaxed);
    if (probed_reduction_id == reduction_id) {
      ReductionIdAndData& red_data = entries_[index];

      Data_t* current_state =
          data_from_message<Data_t>(*red_data.callback_and_data.get_header());
      if (current_state == nullptr) {
        throw Exception{
            "The current data is not set in the internal data but it is in the "
            "slot so it should be set."};
      }
      BinaryOp{}(*current_state, std::forward<Args>(args)...);

      return InsertAction::Combine;
    } else {
      if (probed_reduction_id != 0) {
        // The entry is used by another key.
        continue;
      }
      // We need to synchronize with the `pop()` function to make sure that we
      // don't write to the data before we have acquired it.
      entries_[index].reduction_id.fetch_add(
          reduction_id, std::memory_order::memory_order_acquire);
      entries_[index].callback_and_data = reduction::create_message(
          distributed_object_index, reduction_id,
          Data_t{std::forward<Args>(args)...}, std::move(reduction_callback),
          message_type);
      return InsertAction::Insert;
    }
  }
  return InsertAction::AtCapacity;
}

/*!
 * \brief Manages reduction operations across multiple threads and processes.
 *
 * The Handler class coordinates the collection, combination, and forwarding of
 * reduction data within a node (across threads) and between nodes (across
 * processes). It maintains thread-local storage for reduction data, tracks
 * contributions, and ensures that reduction operations are completed correctly
 * before invoking the associated callback or forwarding the result.
 *
 * Key responsibilities:
 * - Maintains a thread-local `DataHandler` for each worker thread to store and
 *   combine local reduction contributions.
 * - Tracks the number of expected contributions for each reduction operation.
 * - Combines reduction data from different threads and, when complete, prepares
 *   the result for inter-process reduction or callback invocation.
 * - Handles inter-process reduction messages, combining data from different
 *   processes and forwarding results up the process tree.
 * - Provides utilities for setting metadata in reduction messages to track
 *   contributions from different processes and collection elements.
 *
 * Usage:
 * - Each distributed object or collection holds a `Handler` instance to manage
 *   its reductions.
 * - Threads contribute data to reductions via `insert_or_combine()`.
 * - When all expected contributions are received, the `Handler` combines the
 *   data, signaling to the task driver when all data from this process and its
 *   children has been reduced by returning a `Message_t` from
 *   `combine_inter_process()`.
 *
 * Thread safety:
 * - Each thread has its own `DataHandler` for local contributions.
 * - Intra-process reduction data is managed with atomic operations to ensure
 *   safe concurrent access.
 *
 * \see `rts::reduction::DataHandler`
 * \see `rts::reduction::InsertAction`
 * \see `rts::reduction::ReductionCallback`
 */
class Handler {
 public:
  /*!
   * \brief Because of the need for thread-safety, there is no useful case
   * where this class is default-constructed.
   */
  Handler() = delete;

  /*!
   * \brief Constructs a Handler for managing reductions across multiple
   * threads.
   *
   * This constructor initializes the `Handler` with the specified number of
   * threads and the maximum number of simultaneous reductions that can be
   * tracked. It allocates and prepares thread-local `DataHandler` instances and
   * internal storage for inter-process reduction data.
   *
   * \param number_of_threads
   *   The number of threads that will contribute to reductions.
   * \param max_simultaneous_reductions
   *   The maximum number of reductions that can be tracked at the same time.
   *
   * \note
   *   - Each thread gets its own `DataHandler` for thread-local reduction data.
   *   - The `Handler` prepares storage for inter-process reduction handling.
   *   - Throws if `max_simultaneous_reductions` is not a power of two or is
   *     zero.
   */
  Handler(size_t number_of_threads, size_t max_simultaneous_reductions);

  /*!
   * \brief Main internal entry for contributing data to a reduction.
   *
   * Inserts or combines reduction data for a given reduction ID. If the
   * reduction is complete, returns the combined reduction message. Otherwise,
   * returns `std::nullopt`.
   *
   * \tparam BinaryOp
   *   The binary operation used to combine reduction data. An example is
   *   given below.
   * \tparam ComputeExpected
   *   A callable that takes a reduction ID and returns true if the ID is
   *   used in the reduction, false otherwise.
   * \tparam BroadcastAction
   *   The action type for the broadcast.
   * \tparam BroadcastParallelComponent
   *   The parallel component type for the broadcast.
   * \tparam Args
   *   The types of the reduction data arguments.
   *
   * \param compute_expected
   *   Callable that takes a reduction ID and returns true if the ID is
   *   used in the reduction, false otherwise.
   * \param message_type The `MessageType`, either `Reduction` or
   *                     `ReductionOver`
   * \param thread_id
   *   The ID of the thread contributing the data. Must be in the range
   *   `[1, thread_count]`. Thread 0 is reserved for the communication thread.
   * \param distributed_object_index
   *   The index of the distributed object performing the reduction.
   * \param reduction_id
   *   The unique reduction ID for this reduction operation.
   * \param reduction_callback
   *   The callback to invoke after reduction is complete.
   * \param args
   *   The reduction data arguments to be combined.
   *
   * \return
   *   If the reduction is complete, returns the combined reduction `Message_t`.
   *   Otherwise, returns `std::nullopt`.
   *
   * An example of a binary operator is:
   *
   * \snippet Rts/Reduction.cpp rts_reduction_sump_op_functor
   *
   * \throws rts::Exception
   *   - If the reduction ID is zero (reserved as a sentinel value).
   *   - If insertion or combination fails due to a full container or other
   *     error.
   *
   * \note
   *   - This function is thread-safe for concurrent calls from multiple
   *     threads, provided each thread uses a unique `thread_id` in
   *     `[1, thread_count]`.
   *   - The expected number of contributions for each reduction ID must be
   *     correctly provided by `compute_expected`; otherwise, reductions may
   *     never complete or may complete prematurely.
   *   - Only threads that have contributed to the reduction are combined.
   *   - After completion, reduction data is removed from all thread-local
   *     `DataHandlers`.
   *   - The reduction callback is stored in the message and can be retrieved
   *     using `rts::reduction::get_callback()`.
   *   - If the container is full, insertion will fail and
   *     `InsertAction::AtCapacity` will be returned by
   *     `DataHandler::insert_or_combine()`.
   *   - This function only combines data within a single node.
   *
   * \see rts::reduction::DataHandler
   * \see rts::reduction::detail::Counter
   * \see rts::reduction::InsertAction
   * \see rts::reduction::get_callback()
   */
  template <class BinaryOp, class ComputeExpected, class BroadcastAction,
            class BroadcastParallelComponent, class... Args>
  std::optional<Message_t> insert_or_combine(
      const ComputeExpected& compute_expected, MessageType message_type,
      std::uint64_t thread_id, std::uint32_t distributed_object_index,
      std::uint64_t reduction_id,
      ReductionCallback<BroadcastAction, BroadcastParallelComponent>
          reduction_callback,
      Args&&... args);

  /*!
   * \brief Sets metadata in a reduction message over a collection for
   * inter-process reduction.
   *
   * This function configures a reduction message with all necessary metadata
   * for correct routing and aggregation of reduction data across multiple
   * processes. It determines the parent process to which the message should be
   * sent, the expected number of contributions at each process, and handles
   * special cases for the root process. The function also records the source
   * process ID in the message.
   *
   * \tparam ParallelComponent The parallel component type.
   * \tparam Predicate A callable type that takes a collection index and returns
   *         true if the element should be included in the reduction.
   *
   * \param message The reduction message to update.
   * \param process_id The process ID of the current process.
   * \param total_processes The total number of processes in the system.
   * \param elements_on_pid A vector mapping each process ID to a vector of
   *        collection indices present on that process.
   * \param element_predicate Predicate to select which elements participate in
   *        the reduction.
   *
   * \details
   * **Algorithm for tracking inter-process reduction information:**
   *
   * Each process in the system may contribute to a reduction. For a
   * given process \f$n\f$, its first parent is denoted as \f$n_{p1}\f$, and
   * higher-order parents can be found by traversing up the virtual spanning
   * tree across the processes. The algorithm finds the first parent process
   * \f$p_r\f$ that will actually contribute to the reduction. This is also the
   * process to which we send our reduction data. Note that
   * \f$p_r \geq n_{p1}\f$.
   *
   * The parent process \f$p_r\f$ must know how many different processes will
   * send contributions to it directly. This is determined by searching down the
   * subtree rooted at \f$p_r\f$ to find all "orphaned" contributors (i.e.,
   * processes contribute to the reduction but whose direct do not). In a
   * balanced binary tree, each parent typically has two direct contributors:
   * its left and right children. However, since processes may not contribute,
   * a process may receive from many more children down the (sub)tree.
   *
   * The child process only needs to determine which parent will receive its
   * contribution. If the root process (process 0) is reached, the message must
   * also communicate how many contributions are expected from the other half of
   * the tree, ensuring the root knows when the reduction is complete.
   *
   * This function sets the following metadata in the message:
   * - The parent process ID to which the message should be sent.
   * - The expected number of contributions for this reduction at the parent.
   * - The expected number of contributions to the root process (if applicable).
   * - The source process ID.
   *
   * This metadata is used to correctly route and combine reduction messages
   * across the process tree, ensuring that reductions are completed efficiently
   * and correctly in a distributed environment.
   */
  template <class ParallelComponent, class Predicate>
  void set_interprocess_message_info(
      Message_t& message, int process_id, int total_processes,
      const std::vector<std::vector<std::uint64_t>>& elements_on_pid,
      const Predicate& element_predicate) const;

  /*!
   * \brief Sets metadata in a reduction message over a per-process component
   * for inter-process reduction.
   *
   * This function configures a reduction message with the necessary metadata
   * for correct routing and aggregation of reduction data across multiple
   * processes. It determines the parent process to which the message should be
   * sent, the expected number of contributions at each process, and handles
   * special cases for the root process. The function also records the source
   * process ID in the message.
   *
   * \tparam ParallelComponent The parallel component type.
   * \tparam Predicate A callable type that takes a process ID and returns
   *                   true if the process should be included in the reduction.
   *
   * \param message The reduction message to update.
   * \param process_id The process ID of the current process.
   * \param total_processes The total number of processes in the system.
   * \param pid_predicate Predicate to select which processes participate in
   *                      the reduction.
   *
   * \details
   * This function sets the following metadata in the message:
   * - The parent process ID to which the message should be sent.
   * - The expected number of contributions for this reduction at the parent.
   * - The expected number of contributions to the root process (if applicable).
   * - The source process ID.
   *
   * This metadata is used to correctly route and combine reduction messages
   * across the process tree, ensuring that reductions are completed efficiently
   * and correctly in a distributed environment.
   */
  template <class ParallelComponent, class Predicate>
  void set_interprocess_message_info(Message_t& message, const int process_id,
                                     const int total_processes,
                                     const Predicate& pid_predicate) const;

  /*!
   * \brief Combines reduction data from inter-process messages.
   *
   * This function merges the reduction data from a reduction message received
   * from another process with any existing data for the same reduction ID. If
   * the reduction is complete after combining, the resulting combined message
   * is returned. Otherwise, `std::nullopt` is returned.
   *
   * \param message The incoming reduction message to combine.
   * \param p_and_c The parent and children process information for the
   *        reduction.
   * \return The combined reduction message if the reduction is complete, or
   * `std::nullopt` if not yet complete.
   */
  std::optional<Message_t> combine_inter_process(
      Message_t message, rts::detail::ParentAndChildren p_and_c);

 private:
  alignas(rts::hardware_info::hardware_destructive_interference_size)
      detail::Counter reduction_counter_;
  [[maybe_unused]] std::byte cacheline_interference_padding_
      [2 * rts::hardware_info::hardware_destructive_interference_size];
  alignas(rts::hardware_info::hardware_destructive_interference_size)
      std::vector<DataHandler> per_thread_data_handlers_;

  struct InterprocessData {
    std::uint64_t reduction_id{0};
    std::uint64_t count{0};
    Message_t message{};
  };

  alignas(rts::hardware_info::hardware_destructive_interference_size)
      std::vector<InterprocessData> inter_process_entries_{};
};

template <class BinaryOp, class ComputeExpected, class BroadcastAction,
          class BroadcastParallelComponent, class... Args>
std::optional<Message_t> Handler::insert_or_combine(
    const ComputeExpected& compute_expected, const MessageType message_type,
    const std::uint64_t thread_id, const std::uint32_t distributed_object_index,
    const std::uint64_t reduction_id,
    ReductionCallback<BroadcastAction, BroadcastParallelComponent>
        reduction_callback,
    Args&&... args) {
  const InsertAction insert_action =
      per_thread_data_handlers_[thread_id].insert_or_combine<BinaryOp>(
          message_type, distributed_object_index, reduction_id,
          std::move(reduction_callback), std::forward<Args>(args)...);
  if (insert_action == InsertAction::AtCapacity) {
    throw Exception{
        "Reached the maximum number of simultaneous reductions, currently set "
        "to " +
        std::to_string(per_thread_data_handlers_[thread_id].capacity()) +
        ". You can increase this number when constructing the "
        "DistributedTaskDriver."};
  }
  if (reduction_counter_.increment(reduction_id, compute_expected)) {
    Message_t this_thread_data =
        per_thread_data_handlers_[thread_id].pop(reduction_id);
    // Combine all the cases within the node.
    for (size_t i = 1; i < per_thread_data_handlers_.size(); ++i) {
      if (i == thread_id or
          not per_thread_data_handlers_[i].index_of(reduction_id).has_value()) {
        continue;
      }
      detail::combine<BinaryOp, std::tuple<std::decay_t<Args>...>>(
          this_thread_data, per_thread_data_handlers_[i].pop(reduction_id));
    }
    reduction::set_combine_function_pointer(
        this_thread_data,
        &detail::combine<BinaryOp, std::tuple<std::decay_t<Args>...>>);
    reduction::zero_contributed_metadata(this_thread_data);
    reduction::set_contributed_metadata(this_thread_data,
                                        Contribution::self_contributed);
    return this_thread_data;
  }
  return std::nullopt;
}

template <class ParallelComponent, class Predicate>
void Handler::set_interprocess_message_info(
    Message_t& message, const int process_id, const int total_processes,
    const std::vector<std::vector<std::uint64_t>>& elements_on_pid,
    const Predicate& element_predicate) const {
  // Need to set:
  // 1. Parent process to send to.
  // 2. Expected number of contributions to this element, including self. This
  //    gets decremented each time a process contributes until we reach 0.
  // 3. If the parent is the root process and the root process has nobody
  //    contributing, this holds the number of expected contributions to the
  //    root process. On the root process when we receive a reduction message,
  //    we use the first reduction message received to set slot 2.
  // 4. The source process ID.

  const auto pid_predicate = [&elements_on_pid,
                              &element_predicate](const int pid) {
    return std::any_of(
        elements_on_pid[static_cast<size_t>(pid)].begin(),
        elements_on_pid[static_cast<size_t>(pid)].end(),
        [&element_predicate](const std::uint64_t internal_id) {
          return element_predicate(
              rts::detail::from_internal<ParallelComponent>(internal_id));
        });
  };

  const std::int32_t parent_to_send_to =
      process_id == 0 ? -1
                      : rts::detail::find_first_parent(
                            process_id, total_processes, pid_predicate);
  set_target_process_id(message, std::max(parent_to_send_to, 0));
  const std::int32_t number_of_expected_contributions =
      rts::detail::count_first_descendants(process_id, total_processes,
                                           pid_predicate) +
      1;
  set_expected_number_of_contributions(message,
                                       number_of_expected_contributions);
  if (process_id != 0 and
      ((parent_to_send_to == 0 or parent_to_send_to == -1) and
       not pid_predicate(0))) {
    const std::int32_t number_of_expected_contributions_to_root =
        rts::detail::count_first_descendants(0, total_processes, pid_predicate);
    set_expected_number_of_root_contributions(
        message, number_of_expected_contributions_to_root);
  }
  message.get_header()->change_source_process_id(process_id);
}

template <class ParallelComponent, class Predicate>
void Handler::set_interprocess_message_info(
    Message_t& message, const int process_id, const int total_processes,
    const Predicate& pid_predicate) const {
  // Need to set:
  // 1. Parent process to send to.
  // 2. Expected number of contributions to this element, including self. This
  //    gets decremented each time a process contributes until we reach 0.
  // 3. If the parent is the root process and the root process has nobody
  //    contributing, this holds the number of expected contributions to the
  //    root process. On the root process when we receive a reduction message,
  //    we use the first reduction message received to set slot 2.
  // 4. The source process ID.

  const std::int32_t parent_to_send_to =
      process_id == 0 ? -1
                      : rts::detail::find_first_parent(
                            process_id, total_processes, pid_predicate);
  set_target_process_id(message, std::max(parent_to_send_to, 0));
  const std::int32_t number_of_expected_contributions =
      rts::detail::count_first_descendants(process_id, total_processes,
                                           pid_predicate) +
      1;
  set_expected_number_of_contributions(message,
                                       number_of_expected_contributions);
  if (process_id != 0 and
      ((parent_to_send_to == 0 or parent_to_send_to == -1) and
       not pid_predicate(0))) {
    const std::int32_t number_of_expected_contributions_to_root =
        rts::detail::count_first_descendants(0, total_processes, pid_predicate);
    set_expected_number_of_root_contributions(
        message, number_of_expected_contributions_to_root);
  }
  message.get_header()->change_source_process_id(process_id);
  reduction::zero_contributed_metadata(message);
  reduction::set_contributed_metadata(
      message, reduction::Contribution::self_contributed);
}

template <class BroadcastToParallelComponent, class ReductionUnaryPredicate,
          class BroadcastToUnaryPredicate>
size_t size_for_broadcast_to(
    const int process_id, const ReductionUnaryPredicate& reduction_predicate,
    const std::vector<std::vector<std::uint64_t>>& ids_per_process,
    const BroadcastToUnaryPredicate& broadcast_to_predicate) {
  for (int i = 0; i < process_id; ++i) {
    if (reduction_predicate(i)) {
      return 0;
    }
  }
  // None of the processes "before" me (with PID smaller than mine) are
  // contributing to the reduction, which means I am responsible for computing
  // the elements that the broadcast_to is going to.
  size_t number_of_broadcast_to_elements = 0;
  for (const std::vector<std::uint64_t>& ids_on_process : ids_per_process) {
    for (const std::uint64_t id : ids_on_process) {
      if (broadcast_to_predicate(
              rts::detail::from_internal<BroadcastToParallelComponent>(id))) {
        ++number_of_broadcast_to_elements;
      }
    }
  }
  return number_of_broadcast_to_elements;
}

// void add_broadcast_to_ids() {

// }
}  // namespace rts::reduction
