// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mpi.h>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "Rts/Detail/DistributedObjectBase.hpp"
#include "Rts/Exceptions/Exception.hpp"
#include "Rts/IsCollection.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/ParentAndChildren.hpp"
#include "Rts/QuiescenceDetection.hpp"
#include "Rts/ThreadPool.hpp"

namespace rts {
namespace detail {
/*!
 * \brief Converts a user-defined collection index to an internal 64-bit
 * representation.
 *
 * This function reinterprets a user-defined collection index as a 64-bit
 * unsigned integer, which is used internally by the runtime system to represent
 * collection indices in a generic way.
 *
 * \tparam IndexType The type of the user-defined collection index.
 * \param collection_index The user-defined collection index to convert.
 * \return The collection index as a 64-bit unsigned integer.
 *
 * \note The user-defined collection index type must be exactly 64 bits in size.
 * \warning This function uses reinterpret_cast and assumes the memory layout is
 * compatible.
 */
template <class IndexType>
std::uint64_t to_internal(const IndexType& collection_index) {
  return *reinterpret_cast<const std::uint64_t*>(
      std::addressof(collection_index));
}

/*!
 * \brief Converts an internal 64-bit collection index to the user-defined
 * collection index type.
 *
 * This function reinterprets a 64-bit unsigned integer (used internally to
 * represent collection indices) as the user-defined collection index type for
 * the specified parallel component. This is typically used to convert indices
 * stored in a generic format back to their original, user-specified type.
 *
 * \tparam ParallelComponent The parallel component type that defines the
 * collection index type.
 * \param collection_index The internal 64-bit collection index to convert.
 * \return The collection index in the user-defined type for the given parallel
 * component.
 *
 * \note The user-defined collection index type must be exactly 64 bits in size.
 * \warning This function uses `reinterpret_cast` and assumes the memory layout
 * is compatible.
 */
template <class ParallelComponent>
typename ParallelComponent::rts_collection_index from_internal(
    const std::uint64_t& collection_index) {
  return *reinterpret_cast<
      const typename ParallelComponent::rts_collection_index*>(
      std::addressof(collection_index));
}

/*!
 * \brief Counter used to assign each distributed object a unique integer ID.
 *
 * The function `detail::distributed_object_index()` gives the resulting index
 * for a parallel component.
 */
extern uint32_t distributed_object_index_counter;

/*!
 * \brief Returns the unique ID for the parallel component.
 */
template <typename ParallelComponent>
uint32_t distributed_object_index() {
  static uint32_t index = (distributed_object_index_counter++);
  return index;
}

/*!
 * \brief Class used to track argument types and their index in the tuple used
 * to store data.
 */
template <class Arg, size_t Index>
struct ArgIndex {
  using type = Arg;
  static constexpr size_t index = Index;
};
}  // namespace detail

class DistributedTaskDriver {
 public:
  /*!
   * \brief The type of the messages sent by the runtime system.
   *
   * The underlying data is essentially just a byte stream, which is stored in a
   * `std::unique_ptr<char[]>`. There is currently no small message
   * optimization.
   */
  struct Message_t {
    std::unique_ptr<char[]> message{nullptr};

    /// \brief Returns the message header.
    static MessageHeader* get_header(Message_t& message) {
      return reinterpret_cast<MessageHeader*>(message.message.get());
    }

    /// \brief Returns the message header.
    static const MessageHeader* get_header(const Message_t& message) {
      return reinterpret_cast<const MessageHeader*>(message.message.get());
    }

    /// \brief Executes the message.
    static bool execute(
        rts::ThreadPool<Message_t, rts::DistributedTaskDriver*>& /*pool*/,
        const uint32_t thread_id, Message_t& message,
        DistributedTaskDriver* distributed_task_driver) {
      distributed_task_driver->invoke(message, thread_id);
      return true;
    }
  };

  /// \brief Make a copy of Message_t.
  Message_t copy(const DistributedTaskDriver::Message_t& message) const;

  /// \brief The type of the underlying thread pool and dynamic tasking.
  using ThreadPool_t = rts::ThreadPool<Message_t, DistributedTaskDriver*>;

  /// \brief The type used for storing the incoming MPI messages while the NIC
  /// is receiving the data.
  ///
  /// Once a receive is complete, the messages need to be moved to the task
  /// queue.
  using IncomingMpiMessages_t = std::vector<std::tuple<MPI_Request, Message_t>>;

  /// \brief The type used for storing the outgoming MPI messages while the NIC
  /// is sending the data.
  ///
  /// Once a send is complete, the messages need to be freed.
  using OutgoingMpiMessages_t =
      std::vector<std::tuple<std::optional<MPI_Request>, Message_t>>;

  /// @{
  /// \brief Use `create_distributed_task_driver()` to create the task driver
  /// singleton.
  DistributedTaskDriver() = delete;
  DistributedTaskDriver(const DistributedTaskDriver& other) = delete;
  DistributedTaskDriver& operator=(const DistributedTaskDriver& other) = delete;
  DistributedTaskDriver(DistributedTaskDriver&& other) = delete;
  DistributedTaskDriver& operator=(DistributedTaskDriver&& other) = delete;
  ~DistributedTaskDriver() noexcept;
  /// @}

  /*!
   * \brief Launch the threads in the thread pool.
   *
   * By default thread `0` will handle all logging. Passing `std::nullopt`
   * means none of the threads will do logging. Since logging needs to be done
   * in serial for each output method, having one thread responsible for all
   * logging is easiest. This could be generalized if necessary.
   */
  void launch_threads(std::optional<uint32_t> thread_for_logging = 0);

  /// \brief Forces the threads in the thread pool on this process to stop.
  ///
  /// The threads will stop independent of whether or not there are any messages
  /// queued. This means you should only stop the threads once you are certain
  /// quiescence is detected.
  void force_threads_to_stop();

  /// \brief Returns the ID of the current thread.
  std::uint32_t thread_id() const { return thread_id_; }

  /// \brief Returns `true` if the process is locally quiescent.
  ///
  /// Messages from other processes can cause this to no longer be true.
  ///
  /// This function should rarely, if ever, be called explicit. Instead, use
  /// `run_to_quiescence()`.
  bool is_locally_quiescent();

  /// \brief Returns the number of nodes/MPI ranks being used.
  int number_of_nodes() const { return number_of_nodes_; }

  /// \brief The ID of the current node that this is invoked on.
  int current_node_id() const { return my_node_id_; }

  /// \brief The total number of threads being used, including the driver
  /// thread.
  int total_number_of_threads() const { return number_of_threads_; }

  /// \brief The MPI major version being used.
  int mpi_version() const { return mpi_version_; }

  /// \brief The MPI minor/subversion being used.
  int mpi_subversion() const { return mpi_subversion_; }

  /// \brief Wait for the all MPI ranks in the RTS communicator
  ///
  /// Should be used after
  void insert_barrier() const;

  /*!
   * \brief Insert a parallel component into the `DistributedTaskDriver`.
   *
   * This handles registration of the distributed objects without any static
   * variables. This is because we are guaranteed that all distributed objects
   * are registered on all nodes before any node tries to retrieve one based
   * on the type-erased identifier (integer). This is different from
   * actions/member functions of the distributed objects, which can be invoked
   * in an arbitrary order on different objects and so there is no order
   * guarantee, making it necessary to use static variable initialization to
   * guarantee registration before execution.
   */
  template <class ParallelComponent, class... Args>
  void insert_parallel_component(Args&&... args);

  /*!
   * \brief Insert an element of a parallel component collection into the
   * `DistributedTaskDriver`.
   *
   * This must be called for all elements in the collection on all nodes
   * because the `DistributedTaskDriver` keeps track of where different
   * elements are to make communication easier for users.
   *
   * Collection parallel components must have a type alias
   * `rts_collection_index` that is the user-facing index. This type must be
   * exactly 64 bits in size and the user must explicitly set all bits. Any
   * bits that are "unused" must be set to 0 otherwise the behavior is
   * undefined.
   *
   * See `insert_parallel_component()` for details about registration.
   */
  template <class ParallelComponent, class... Args>
  void insert_parallel_component_collection(
      const typename ParallelComponent::rts_collection_index& user_index,
      int node_to_insert_on, Args&&... args);

  /*!
   * \brief The MPI driver run on the main thread for a single phase of the
   * evolution.
   *
   * A phase is ended when quiescence is reached.
   *
   * This function has a busy loop that does the following in order:
   * 1. Probe for up to `N_{in}` incoming MPI messages and start their receives.
   * 2. Check for up to `N_{out_msg}` and begin the send operations.
   * 3. Check if any incoming MPI messages have completed, and if so add the
   *    tasks to the queue. If we moved any then we will not do a check for
   *    quiescence.
   * 4. Check if any of the sent messages have completed and can be removed.
   * 5. Perform a quiescence check as described below.
   *
   * ### Quiescence detection
   *
   * See rts::qd::Local and rts::qd::Global for documentation of the QD
   * algorithms used.
   *
   */
  void run_to_quiescence(int max_to_receive = 10, int max_to_send = 10);

  /*!
   * \brief Invokes the `Action` on the `ParallelComponent`.
   *
   * Allows invoking/calling `Actions` on local or remote parallel
   * components. If the parallel component is a collection then
   * `user_index_or_target_node` must be a 64-it user index of type
   * `ParallelComponent::rts_collection_index`. If the parallel component is a
   * regular component, then `user_index_or_target_node` must be the node on
   * which the action should be invoked.
   *
   * The `args...` are the argument with which the member function
   * `threaded_action` on the receiving parallel component will be
   * invoked. An example of a threaded action member function of a parallel
   * component is
   * ```cpp
   * template <class Action, class... Args>
   * void threaded_action(rts::DistributedTaskDriver& driver, Args... args);
   * ```
   * Specialization to specific actions is essentially providing remotely
   * callable member functions. For example,
   * ```cpp
   * template <>
   * void threaded_action<MyAction>(rts::DistributedTaskDriver& task_driver,
   *                                const int t)
   * ```
   * provides remotely callable member function labeled or tagged by the
   * "action" struct/class/type, `MyAction` in this case.
   */
  template <class Action, class ParallelComponent, class IndexType,
            class... Args>
  void invoke(const IndexType& user_index_or_target_node, Args&&... args);

  /*!
   * \brief Broadcasts `args` to all members of the parallel component
   * (collection) and invokes the `Action`.
   *
   * Allows invoking/calling `Actions` on all elements of a (collection)
   * parallel component. The arguments `args...` are forwarded to the member
   * function `threaded_action` on each receiving distributed object.
   *
   * An example of a threaded action member function of a parallel component is:
   * ```cpp
   * template <class Action, class... Args>
   * void threaded_action(rts::DistributedTaskDriver& driver, Args... args);
   * ```
   * Specialization to specific actions is essentially providing remotely
   * callable member functions. For example,
   * ```cpp
   * template <>
   * void threaded_action<MyAction>(rts::DistributedTaskDriver& task_driver,
   *                                const int t)
   * ```
   * provides a remotely callable member function labeled or tagged by the
   * "action" struct/class/type, `MyAction` in this case.
   *
   * \tparam Action The action invoked on each distributed object.
   * \tparam ParallelComponent The parallel component to broadcast to.
   * \param args The arguments to sent to each distributed object.
   *
   * \throws Exception If any argument is a raw pointer or C-style array.
   */
  template <class Action, class ParallelComponent, class... Args>
  void broadcast(Args&&... args);

 private:
  // The DistributedTaskDriver can only be created using the
  // create_distributed_task_driver() function.
  DistributedTaskDriver(bool finalize_mpi, bool mpi_supports_multithreading);

  /// \cond
  friend DistributedTaskDriver& create_distributed_task_driver(
      int* argc, char** argv[], bool initialize_mpi);

  template <class ParallelComponent>
  friend ParallelComponent* local_parallel_component(
      DistributedTaskDriver& distributed_task_driver);

  template <class ParallelComponent, class IndexType>
  friend ParallelComponent* local_parallel_component(
      DistributedTaskDriver& distributed_task_driver,
      const IndexType& user_index);
  /// \endcond

  static std::string mpi_threading_to_string(int mpi_threading);

  // The anchor function is used to compute relative pointers to member
  // functions that invoke actions.
  void anchor() {}

  /*!
   * \brief Provide an infinite loop to attach a debugger during startup. Useful
   * for debugging MPI runs.
   *
   * Each MPI rank prints out name `rts_pid_#_host_NAME` to the working
   * directory. This allows you to attach GDB to the running process using
   * `gdb --pid=PID`, once for each MPI rank. You must then halt the program
   * using `C-c` and then call `set var i = 7` inside GDB. Once you've done this
   * on each MPI rank, you can have each MPI rank `continue`.
   *
   * To add support for attaching to a debugger in an executable, you must add
   * `driver.attach_debugger()` to the start of the executable after you call
   * `rts::create_distributed_task_driver()`. Then, when you launch the
   * executable launch it as
   * ```shell
   * RTS_ATTACH_DEBUGGER=1 mpirun -np N ...
   * ```
   * The environment variable `RTS_ATTACH_DEBUGGER` being set tells the code to
   * allow attaching from a debugger.
   */
  void attach_debugger();

  /*!
   * \brief Invokes the action encoded in `message` on the thread with ID
   * `thread_id`.
   *
   * This uses `threaded_action_absolute_ptr` to get which threaded action
   * overload needs to be called and invokes it.
   */
  void invoke(Message_t& message, uint32_t thread_id);

  /*!
   * \brief Send the message to the target node.
   *
   * If the target node is the current node then the message is inserted into
   * the local message queue. If the target is remote then the message is
   * inserted into the outgoing message queue.
   */
  void send_data(int target_node, Message_t message);

  /*!
   * \brief Sends a message using non-blocking MPI.
   *
   * This function initiates a non-blocking MPI send (MPI_Isend) of the provided
   * message. It  manages the MPI request, and updates quiescence detection
   * counters.
   *
   * \param in_message The message to be sent.
   *
   * \throws Exception if `in_message.message` is a nullptr
   * \throws Exception if the destination process is outside the range [0,
   *         number_of_nodes).
   * \throws Exception if the message has a negative number of bytes.
   * \throws MpiException If MPI operations fail.
   *
   * \warning This function is not thread safe.
   */
  void send_message_impl(Message_t in_message);

  /// invoke_impl is invoked _by_ the thread pool on the task driver to
  /// initiate the action on the distributed action.
  template <class Action, class ParallelComponent, class... ArgIndexes>
  void threaded_action_impl(Message_t& message);

  // Compute the threaded action member function pointer location relative to
  // the anchor() member function pointer. This is then sent to other nodes.
  template <class Action, class ParallelComponent, class... Args, size_t... Is>
  detail::MemberFunctionPtr threaded_action_relative_ptr(
      std::index_sequence<Is...> /*meta*/) {
    return {detail::to_member_function_ptr<void, DistributedTaskDriver>(
                &DistributedTaskDriver::template threaded_action_impl<
                    Action, ParallelComponent, detail::ArgIndex<Args, Is>...>) -
            detail::to_member_function_ptr<void, DistributedTaskDriver>(
                &DistributedTaskDriver::anchor)};
  }

  // Compute the threaded action member function pointer absolute address from
  // the address relative to the anchor() function.
  auto threaded_action_absolute_ptr(
      const detail::MemberFunctionPtr& theaded_action_rel_ptr)
      -> void (DistributedTaskDriver::*)(Message_t&) {
    return detail::from_member_function_ptr<void, DistributedTaskDriver,
                                            Message_t&>(
        {theaded_action_rel_ptr +
         detail::to_member_function_ptr<void, DistributedTaskDriver>(
             &DistributedTaskDriver::anchor)});
  }

  /*!
   * \brief Initiate several sends to remote distributed objects.
   *
   * At most `max_to_send` sends are initiated. These sends are done via a
   * non-blocking `MPI_Isend`.
   *
   * #### Implementation notes
   *
   * We dequeue in bulk from the `outgoing_messages_` since it is more
   * efficient than to do them individually. We do at most 10 at a time up to
   * `max_to_send`. The resulting `MPI_Request`s for the sends are stored in
   * the member variable `outgoing_mpi_messages_`, which is periodically
   * cleaned by the function `clean_outgoing_mpi_messages()`.
   *
   * This function is not threadsafe.
   */
  void initiate_sends(int max_to_send);

  /*!
   * \brief Sends a copy of the given message to each child process in the
   * process tree.
   *
   * This function determines the left and right child process IDs of the
   * current process and sends a copy of the provided message to each child that
   * exists (i.e., whose process ID is not -1). The destination process ID in
   * the message header is updated for each child before sending.
   *
   * If both left and right children exist and have the same process ID, an
   * exception is thrown.
   *
   * \param message The message to be sent to each child process. The message is
   * copied for each child.
   *
   * \throws Exception if the left and right child process IDs are the same and
   * not -1.
   *
   * \note If neither child exists, this function does nothing.
   */
  void send_to_children(const Message_t& message);

  /*!
   * \brief Cleans up any completed outgoing MPI messages.
   *
   * This function is not threadsafe.
   */
  void clean_outgoing_mpi_messages();

  /*!
   * \brief Initial several receives from remote distributed objects.
   *
   * At most `max_to_receive` sends are initiated. These receives are done via
   * a non-blocking `MPI_Irecv`.
   *
   * #### Implementation notes
   *
   * We initiate up to `max_to_receive` receives as `MPI_Irecv`. We first
   * probe for an incoming message from an MPI rank. If there is one then we
   * allocate sufficient memory and store the message and `MPI_Request` in the
   * member variable `incoming_mpi_messages_`.
   *
   * This function is not threadsafe.
   */
  void initiate_receives(int max_to_receive);

  /*!
   * \brief Checks for incoming MPI messages that have been received.
   *
   * The messages are added to the task pool in bulk.
   *
   * This function is not threadsafe.
   */
  void clean_incoming_mpi_messages();

  /*!
   * \brief Expands a broadcast message into individual invoke messages for all
   * local elements of a collection or regular component.
   *
   * This function takes a broadcast message and, for each local element of the
   * targeted collection  (or for a regular component), creates a copy of the
   * message, updates its header to convert it into an invoke message, and
   * appends it to the provided task vector. The function ensures that only
   * elements local to the current node are targeted.
   *
   * For collection components, the function iterates over all collection
   * elements, and for each element that resides on the current node, it creates
   * and appends an invoke message. For regular components, a single invoke
   * message is created and appended.
   *
   * \param[out] all_tasks The vector to which the generated invoke messages
   * will be appended.
   * \param[in] message The original broadcast message to be expanded for local
   * processing.
   *
   * \throws Exception If the distributed object index is out of range, or if
   * the variant type of the distributed object is not supported for broadcasts.
   *
   * \note Only collection and regular components are currently supported for
   * broadcast expansion. Other variant types will result in an exception.
   */
  void add_local_broadcast_tasks(std::vector<Message_t>& all_tasks,
                                 const Message_t& message) const;
  /*!
   * \brief The type used to store each distributed object or collection.
   *
   * We use a `std::variant` of `std::unique_ptr` so that it is clear if we
   * should be indexing into a collection or not. Essentially, this is used to
   * maximize the chance of catching subtle errors since indexing a magic number
   * in the map is not guaranteed to be safe.
   */
  struct DistributedOjectClassHolder {
    struct CollectionHolder {
      int node_id = -1;
      std::unique_ptr<detail::DistributedObjectBase> object = nullptr;
    };

    using Map_t = std::unordered_map<uint64_t, CollectionHolder>;

    DistributedOjectClassHolder(
        std::unique_ptr<detail::DistributedObjectBase> in_object,
        std::string in_name)
        : objects(std::move(in_object)), name(std::move(in_name)) {}

    DistributedOjectClassHolder(
        std::unordered_map<uint64_t, CollectionHolder> in_objects,
        std::string in_name)
        : objects(std::move(in_objects)), name(std::move(in_name)) {}

    using variant_t =
        std::variant<std::unique_ptr<detail::DistributedObjectBase>, Map_t>;
    variant_t objects;
    std::string name;
    int number_of_local_objects{-1};
  };

  MPI_Comm rts_comm_{};
  bool finalize_mpi_{false};
  bool mpi_supports_multithreading_{false};
  int number_of_nodes_{0};
  int my_node_id_{std::numeric_limits<int>::max()};
  int number_of_threads_{0};
  int mpi_version_{0};
  int mpi_subversion_{0};
  int node_id_for_receive_{0};
  // Note: the thread_id_ is set upon entry from the thread pool.
  // We have an offset of 1 because we consider thread 0 on the process to be
  // the communication thread.
  static constexpr std::uint32_t thread_id_offset_ = 1;
  static thread_local std::uint32_t thread_id_;

  std::unique_ptr<ThreadPool_t> thread_pool_{};
  std::vector<DistributedOjectClassHolder> distributed_objects_;

  detail::ParentAndChildren parent_and_children_{};
  IncomingMpiMessages_t incoming_mpi_messages_{};
  OutgoingMpiMessages_t outgoing_mpi_messages_{};
  moodycamel::ConcurrentQueue<std::tuple<int, Message_t>> outgoing_messages_{};
  qd::Global global_qd_{};
  static constexpr int local_qd_counts_for_global_qd_ = 50;

  // Special values used for different types of messages.
  static constexpr int broadcast_process_id = -1;
};

template <class ParallelComponent, class... Args>
void DistributedTaskDriver::insert_parallel_component(Args&&... args) {
  static_assert(
      not rts::is_collection_v<ParallelComponent>,
      "To insert into a collection use insert_parallel_component_collection");
  const auto index = rts::detail::distributed_object_index<ParallelComponent>();
  if (index < distributed_objects_.size()) {
    throw Exception(
        "Inserting a parallel component that was already inserted with index " +
        std::to_string(index) + " and name " + ParallelComponent::name());
  }
  distributed_objects_.emplace_back(
      std::unique_ptr<detail::DistributedObjectBase>{
          std::make_unique<ParallelComponent>(std::forward<Args>(args)...)},
      ParallelComponent::name());
  distributed_objects_.back().number_of_local_objects = 1;
  if (index + 1 != distributed_objects_.size()) {
    throw Exception("The index " + std::to_string(index) +
                    " of the parallel component " + ParallelComponent::name() +
                    " that was computed by the function "
                    "distributed_object_index does not match the entry of the "
                    "distributed_objects_ vector " +
                    std::to_string(distributed_objects_.size() - 1) +
                    " on MPI rank " + std::to_string(my_node_id_));
  }
}

template <class ParallelComponent, class... Args>
void DistributedTaskDriver::insert_parallel_component_collection(
    const typename ParallelComponent::rts_collection_index& user_index,
    const int node_to_insert_on, Args&&... args) {
  static_assert(
      rts::is_collection_v<ParallelComponent>,
      "To insert into a collection use insert_parallel_component_collection");
  static_assert(sizeof(typename ParallelComponent::rts_collection_index) ==
                sizeof(std::uint64_t));
  const auto index = rts::detail::distributed_object_index<ParallelComponent>();
  using Map =
      std::variant_alternative_t<1, DistributedOjectClassHolder::variant_t>;
  if (index == distributed_objects_.size()) {
    // If we do not have the distributed object collection already inserted,
    // insert it.
    distributed_objects_.emplace_back(Map{}, ParallelComponent::name());
    distributed_objects_.back().number_of_local_objects = 0;
  }
  if (index + 1 != distributed_objects_.size()) {
    throw Exception("The index " + std::to_string(index) +
                    " of the parallel component " + ParallelComponent::name() +
                    " that was computed by the function "
                    "distributed_object_index does not match the entry of the "
                    "distributed_objects_ vector " +
                    std::to_string(distributed_objects_.size() - 1) +
                    " on MPI rank " + std::to_string(my_node_id_));
  }
  Map& collection = std::get<1>(distributed_objects_[index].objects);
  const std::uint64_t collection_index = detail::to_internal(user_index);
  if (collection.find(collection_index) != collection.end()) {
    std::stringstream ss;
    ss << user_index;
    const std::string index_name = ss.str();
    throw Exception("Inserting an already existing collection index " +
                    index_name + " into the collection " +
                    ParallelComponent::name());
  }

  if (node_to_insert_on == my_node_id_) {
    collection.emplace(std::pair{
        collection_index,
        DistributedOjectClassHolder::CollectionHolder{
            node_to_insert_on, std::unique_ptr<detail::DistributedObjectBase>{
                                   std::make_unique<ParallelComponent>(
                                       std::forward<Args>(args)...)}}});
    ++distributed_objects_[index].number_of_local_objects;
  } else if (node_to_insert_on >= number_of_nodes_) {
    throw Exception("Cannot insert collection " + ParallelComponent::name() +
                    " into node " + std::to_string(node_to_insert_on) +
                    " because we only have " +
                    std::to_string(number_of_nodes_) + " nodes.\n");
  } else {
    // Insert for tracking which node this collection element is on.
    collection.emplace(std::pair{
        collection_index,
        DistributedOjectClassHolder::CollectionHolder{
            node_to_insert_on,
            std::unique_ptr<detail::DistributedObjectBase>{nullptr}}});
  }
}

template <class Action, class ParallelComponent, class IndexType, class... Args>
void DistributedTaskDriver::invoke(const IndexType& user_index_or_target_node,
                                   Args&&... args) {
  static_assert(((not(std::is_pointer_v<std::decay_t<Args>> or
                      std::is_array_v<std::decay_t<Args>>)) &&
                 ...),
                "We cannot serialize raw pointers or C-style arrays in a "
                "safe manner. Please wrap these in a container that can "
                "safely handle the serialization.");

  int target_node = -1;
  std::uint64_t collection_index = MessageHeader::no_collection_index();
  if constexpr (rts::is_collection_v<ParallelComponent>) {
    static_assert(
        std::is_same_v<typename ParallelComponent::rts_collection_index,
                       IndexType>);
    const auto object_index =
        rts::detail::distributed_object_index<ParallelComponent>();
    collection_index = detail::to_internal(user_index_or_target_node);
    try {
      DistributedOjectClassHolder::Map_t& collection =
          std::get<1>(distributed_objects_[object_index].objects);
      try {
        target_node = collection.at(collection_index).node_id;
      } catch (const std::exception& e) {
        std::stringstream ss;
        ss << user_index_or_target_node;
        throw Exception{
            "Failed to retrieve collection element " + ss.str() +
            " from collection parallel component " + ParallelComponent::name() +
            ". Exception being handle has message: " + std::string{e.what()}};
      }
    } catch (const std::exception& e) {
      std::stringstream ss;
      ss << user_index_or_target_node;
      throw Exception{"Failed to retrieve the parallel component '" +
                      ParallelComponent::name() +
                      "' as a collection. This is likely an internal bug. "
                      "Please file an issue with steps on how to reproduce."};
    }
  } else {
    static_assert(std::is_same_v<IndexType, int>);
    target_node = user_index_or_target_node;
  }

  if (target_node == my_node_id_ or
      ((std::is_trivially_copyable_v<std::decay_t<Args>> && ...))) {
    // Regardless of whether or not we are crossing an address space
    // boundary we cannot store or forward references in the Data, we can
    // only safely store values.
    using Data_t = std::tuple<std::decay_t<Args>...>;
    const std::uint32_t data_offset =
        sizeof(MessageHeader)
        // Add extra bytes to make sure we can align Data_t
        // properly. We compute the remainder of the MessageHeader size and
        // the alignment of the data. This would give us, e.g. 5 bytes, which
        // means we have e.g. 37 bytes for MessageHeader. The amount we
        // would need to align then is given by the C++:
        + (alignof(Data_t) - sizeof(MessageHeader) % alignof(Data_t));
    const std::uint64_t buffer_size = data_offset
                                      // Add the size of the data type (tuple)
                                      + sizeof(Data_t);
    std::unique_ptr<char[]> buffer{new (std::align_val_t(
        std::max(alignof(MessageHeader), alignof(Data_t)))) char[buffer_size]};

    MessageHeader* message = new (buffer.get())
        MessageHeader{threaded_action_relative_ptr<Action, ParallelComponent,
                                                   std::decay_t<Args>...>(
                          std::make_index_sequence<sizeof...(Args)>{}),
                      collection_index,
                      buffer_size,
                      detail::distributed_object_index<ParallelComponent>(),
                      data_offset,
                      current_node_id(),
                      target_node,
                      global_qd_.local_sweep_number(),
                      false,
                      MessageType::Invoke};
    Data_t* data_location = rts::create_data_in_message<Data_t>(*message);

    *data_location = Data_t{std::forward<Args>(args)...};
    send_data(target_node, {std::move(buffer)});
  } else {
    throw Exception{"Serialization in invoke() is not yet implemented."};
    // send_data(target_node, std::move(buffer));
  }
}

template <class Action, class ParallelComponent, class... Args>
void DistributedTaskDriver::broadcast(Args&&... args) {
  static_assert(((not(std::is_pointer_v<std::decay_t<Args>> or
                      std::is_array_v<std::decay_t<Args>>)) &&
                 ...),
                "We cannot serialize raw pointers or C-style arrays in a "
                "safe manner. Please wrap these in a container that can "
                "safely handle the serialization.");
  if ((std::is_trivially_copyable_v<std::decay_t<Args>> && ...)) {
    // Regardless of whether or not we are crossing an address space
    // boundary we cannot store or forward references in the Data, we can
    // only safely store values.
    using Data_t = std::tuple<std::decay_t<Args>...>;
    const std::uint32_t data_offset =
        sizeof(MessageHeader)
        // Add extra bytes to make sure we can align Data_t
        // properly. We compute the remainder of the MessageHeader size and
        // the alignment of the data. This would give us, e.g. 5 bytes, which
        // means we have e.g. 37 bytes for MessageHeader. The amount we
        // would need to align then is given by the C++:
        + (alignof(Data_t) - sizeof(MessageHeader) % alignof(Data_t));
    const std::uint64_t buffer_size = data_offset
                                      // Add the size of the data type
                                      + sizeof(Data_t);
    std::unique_ptr<char[]> buffer{new (std::align_val_t(
        std::max(alignof(MessageHeader), alignof(Data_t)))) char[buffer_size]};

    MessageHeader* message = new (buffer.get())
        MessageHeader{threaded_action_relative_ptr<Action, ParallelComponent,
                                                   std::decay_t<Args>...>(
                          std::make_index_sequence<sizeof...(Args)>{}),
                      MessageHeader::no_collection_index(), buffer_size,
                      detail::distributed_object_index<ParallelComponent>(),
                      data_offset, current_node_id(),
                      // For broadcasts we first set the target process ID to
                      // self, then update it as we send to different processes.
                      current_node_id(), global_qd_.local_sweep_number(), false,
                      MessageType::Broadcast};
    Data_t* data_location = rts::create_data_in_message<Data_t>(*message);

    *data_location = Data_t{std::forward<Args>(args)...};
    send_data(broadcast_process_id, {std::move(buffer)});
  } else {
    throw Exception{"Serialization in broadcast() is not yet implemented."};
    // send_data(broadcast_process_id, std::move(buffer));
  }
}

template <class Action, class ParallelComponent, class... ArgIndexes>
void DistributedTaskDriver::threaded_action_impl(Message_t& message) {
  MessageHeader* header = Message_t::get_header(message);
  if (header->distributed_object_index() >= distributed_objects_.size()) {
    throw rts::Exception{"Requested distributed object with index " +
                         std::to_string(header->distributed_object_index()) +
                         " but only have " +
                         std::to_string(distributed_objects_.size())};
  }

  using Data_t = std::tuple<typename ArgIndexes::type...>;
  Data_t args_data{};
  Data_t* args = nullptr;
  if (header->data_was_serialized()) {
    args = std::addressof(args_data);
    (void)std::initializer_list<char>{[&args_data, &message]() {
      (void)message;
      [[maybe_unused]] auto& t = std::get<ArgIndexes::index>(args_data);
      // TODO: deserialize
      throw Exception{"Not implemented"};
      return '0';
    }()...};
  } else {
    args = data_from_message<Data_t>(*header);
  }
  if constexpr (rts::is_collection_v<ParallelComponent>) {
    dynamic_cast<ParallelComponent&>(
        *std::get<1>(
             distributed_objects_[header->distributed_object_index()].objects)
             .at(header->target_collection_index())
             .object)
        .template threaded_action<Action>(
            *this, std::move(std::get<ArgIndexes::index>(*args))...);
  } else {
    dynamic_cast<ParallelComponent&>(
        *std::get<0>(
            distributed_objects_[header->distributed_object_index()].objects))
        .template threaded_action<Action>(
            *this, std::move(std::get<ArgIndexes::index>(*args))...);
  }
}

/// \brief Retrieve a pointer to the local parallel component.
///
/// If the pointer is null then the object could not be retrieved. This can
/// never occur in practice.
template <class ParallelComponent>
ParallelComponent* local_parallel_component(
    DistributedTaskDriver& distributed_task_driver) {
  static_assert(not rts::is_collection_v<ParallelComponent>);
  const auto object_index =
      detail::distributed_object_index<ParallelComponent>();
  if (object_index >= distributed_task_driver.distributed_objects_.size()) {
    throw rts::Exception{"Requested distributed object with index " +
                         std::to_string(object_index) + " and name " +
                         ParallelComponent::name() + " was never inserted."};
  }
  return dynamic_cast<ParallelComponent*>(
      std::get<0>(distributed_task_driver.distributed_objects_[object_index])
          .get());
}

/// \brief Retrieve a pointer to the local parallel component of a specific
/// member of a collection.
///
/// If the pointer is null then the object could not be retrieved because the
/// requested element is not on this node.
template <class ParallelComponent, class IndexType>
ParallelComponent* local_parallel_component(
    DistributedTaskDriver& distributed_task_driver,
    const IndexType& user_index) {
  static_assert(rts::is_collection_v<ParallelComponent>);
  static_assert(sizeof(IndexType) == sizeof(uint64_t));
  const auto object_index =
      detail::distributed_object_index<ParallelComponent>();
  if (object_index >= distributed_task_driver.distributed_objects_.size()) {
    throw rts::Exception{"Requested distributed object with index " +
                         std::to_string(object_index) + " and name " +
                         ParallelComponent::name() + " was never inserted."};
  }
  const std::uint64_t collection_index = detail::to_internal(user_index);
  auto& object_collection =
      std::get<1>(distributed_task_driver.distributed_objects_[object_index]);
  auto& object_it = object_collection.find(collection_index);
  if (object_it == object_collection.end()) {
    return nullptr;
  }
  return dynamic_cast<ParallelComponent*>(object_it->second.get());
}

/// \brief Create the DistributedTaskDriver::the_driver object that can be
/// used to globally access the task driver.
DistributedTaskDriver& create_distributed_task_driver(int* argc, char** argv[],
                                                      bool initialize_mpi);
}  // namespace rts
