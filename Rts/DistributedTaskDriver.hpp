// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <mpi.h>
#include <new>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "Rts/Detail/ActiveObject.hpp"
#include "Rts/Detail/AllElements.hpp"
#include "Rts/Detail/DistributedObjectBase.hpp"
#include "Rts/Detail/DistributedObjectIndex.hpp"
#include "Rts/Detail/GetOutput.hpp"
#include "Rts/Detail/IndexConversion.hpp"
#include "Rts/DistributedObjectIndex.hpp"
#include "Rts/Exceptions/Exception.hpp"
#include "Rts/IsCollection.hpp"
#include "Rts/Message.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/MessageType.hpp"
#include "Rts/ParentAndChildren.hpp"
#include "Rts/QuiescenceDetection.hpp"
#include "Rts/Reduction.hpp"
#include "Rts/ThreadPool.hpp"

namespace rts {
namespace detail {
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
 private:
  friend struct Message_t;
  struct DistributedOjectClassHolder;

  /*!
   * \brief Holds a single element of a distributed object collection.
   *
   * The CollectionHolder struct is used internally by DistributedTaskDriver
   * to represent an element of a collection parallel component. It stores
   * the process where the collection element resides, as well
   * as a unique pointer to the actual distributed object instance.
   *
   * CollectionHolder is primarily used as the value type in the map that
   * tracks all elements of a collection parallel component, allowing the
   * runtime to efficiently locate and manage distributed objects across
   * processes.
   *
   * \note The object pointer may be nullptr for elements that are not local
   *       to the current node.
   */
  struct CollectionHolder {
   public:
    /*!
     * \brief The ID of the process on which this collection element is located.
     */
    int process_id = -1;

   private:
    friend DistributedTaskDriver;
    friend DistributedOjectClassHolder;
    template <class ParallelComponent, class IndexType>
    friend ParallelComponent* local_parallel_component(
        DistributedTaskDriver& distributed_task_driver,
        const IndexType& user_index);

    CollectionHolder(const int in_process_id,
                     std::unique_ptr<detail::DistributedObjectBase> in_object)
        : process_id(in_process_id), object(std::move(in_object)) {}

    std::unique_ptr<detail::DistributedObjectBase> object = nullptr;
  };

 public:
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

  /*!
   * \brief Exit insert mode and synchronize component registration across
   * ranks.
   *
   * After all processes have called insert_parallel_component() or
   * insert_parallel_component_collection(), this function leaves insert mode
   * and ensures that all ranks registered the same set of parallel components
   * and collection elements in the same order.
   *
   * If `check_consistency_across_processes` is true, each process serializes
   * its component accounting data and exchanges it with all other processes to
   * check for mismatches. Then a barrier on the RTS communicator ensures
   * that all ranks reach the same execution point.
   *
   * \param check_consistency_across_processes If true, perform a cross-rank
   *        consistency check before the barrier.
   *
   * \throws rts::MpiException If any MPI call (probe, recv, barrier) fails
   *         during the consistency check or barrier.
   * \throws rts::Exception If component accounting differs across ranks.
   */
  void insert_barrier(bool check_consistency_across_processes = true) const;

  /// \brief Synchronize all processes at this function call.
  ///
  /// When `barrier()` returns, all processes are guaranteed to have completed
  /// to this point.
  void barrier() const;

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
   * \brief Removes an element from a collection parallel component.
   *
   * This function removes a specific element, identified by its collection
   * index, from a collection parallel component that was previously inserted
   * into the DistributedTaskDriver. The element is removed from both the
   * internal collection mapping and the list of collection indices for the
   * corresponding process.
   *
   * \tparam ParallelComponent The collection parallel component type.
   * \param user_index The collection index of the element to remove.
   *
   * \throws Exception if the parallel component is not registered, is not a
   *         collection, or if the specified element does not exist.
   *
   * \note This function must be called in insert mode (before
   * insert_barrier()). All processes must remove the same elements to maintain
   * consistency.
   */
  template <class ParallelComponent>
  void remove_parallel_component_collection(
      const typename ParallelComponent::rts_collection_index& user_index);

  /*!
   * \brief Returns a vector of vectors containing the collection indices for
   * each process.
   *
   * For a given collection parallel component, this function returns a
   * reference to a vector where each element corresponds to a process, and
   * contains a vector of collection indices (as `uint64_t`) that reside on that
   * process.
   *
   * This allows querying which collection elements are present on each process.
   *
   * \tparam ParallelComponent The collection parallel component.
   * \return A const reference to a vector of vectors of collection indices per
   * process.
   * \throws Exception if the parallel component is not registered or is not a
   * collection.
   */
  template <class ParallelComponent>
  auto collection_ids_on_processes() const
      -> const std::vector<std::vector<std::uint64_t>>&;

  /*!
   * \brief Returns a vector of collection indices for a specific process.
   *
   * For a given collection parallel component and process ID, this function
   * returns a reference to the vector of collection indices (as `uint64_t`)
   * that reside on the specified process.
   *
   * This allows querying which collection elements are present on a particular
   * process.
   *
   * \tparam ParallelComponent The collection parallel component.
   * \tparam Integer The type of the process ID (must be integral).
   * \param pid The process ID to query.
   * \return A const reference to the vector of collection indices for the given
   * process.
   * \throws Exception if the parallel component is not registered or is not a
   * collection.
   */
  template <class ParallelComponent, class Integer>
  auto collection_ids_on_process(const Integer& pid) const
      -> const std::vector<std::uint64_t>&;

  /*!
   * \brief Returns a map of collection indices to their location and object
   * holder.
   *
   * This function provides access to the internal mapping from collection
   * indices to their corresponding CollectionHolder for a given collection
   * parallel component. Each entry in the returned map associates a collection
   * index (as a uint64_t) with a CollectionHolder, which contains the
   * process ID (`.process_id`) where the element resides.
   *
   * The `.process_id` field of each CollectionHolder indicates the process ID
   * that owns the collection element. This allows users and internal code to
   * determine the location of each collection element across the distributed
   * system.
   *
   * Usage example:
   * \snippet Rts/DistributedTaskDriver.cpp collection_ids_and_locations_usage
   *
   * \tparam ParallelComponent The collection parallel component type.
   * \return A const reference to the map from collection indices to
   *         CollectionHolder.
   * \throws Exception if the parallel component is not registered or is not a
   * collection.
   */
  template <class ParallelComponent>
  const std::unordered_map<std::uint64_t, CollectionHolder>&
  collection_ids_and_locations() const;

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

  /*!
   * \brief Broadcasts `args` to a filtered subset of elements in a collection
   * parallel component and invokes the `Action`.
   *
   * This function allows invoking/calling `Actions` on a subset of elements of
   * a collection parallel component, as selected by a user-provided predicate.
   * The arguments `args...` are forwarded to the member function
   * `threaded_action` on each receiving distributed object that matches the
   * predicate.
   *
   * The predicate must be callable with a collection index of type
   * `ParallelComponent::rts_collection_index` and return a `bool` indicating
   * whether the element should receive the broadcast.
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
   * \tparam ParallelComponent The collection parallel component to broadcast
   *         to.
   * \tparam UnaryPredicate The predicate type used to filter collection
   *         elements.
   * \tparam Args The argument types to send to each distributed
   *         object.
   * \param predicate A callable that takes a collection index and
   *        returns true if the element should receive the broadcast.
   * \param args The arguments to send to each distributed object.
   *
   * \throws Exception If any argument is a raw pointer or C-style array, if the
   *                  predicate is invalid, if the parallel component is not
   *                  registered, or on internal errors.
   *
   * \note Only trivially copyable arguments are currently supported.
   *       Serialization for non-trivially copyable types is not yet
   *       implemented.
   *
   * Implementation details
   *
   * We first construct a single copy of the serialized data in a buffer. This
   * serialized data will be copied into each message. Doing so once minimizes
   * the overhead of the serialization process itself and requires us to only
   * use a `std::memcpy()`.
   *
   * Next we compute how many collection IDs to send to each process. We do
   * this without any memory allocations by having a member variable that is a
   * `vector<vector<int>>`. The outer vector is of the size of the number of
   * threads while the inner vector is the size of the number of processes. We
   * use the function `compute_elements_per_pid()` to compute the number of
   * elements on each process. Note that the vectors are allocated in the
   * constructor and this implementation is to avoid any additional memory
   * allocations.
   *
   * Next we create a `vector<Message_t>` that is of size
   * `number_of_nodes()+number_of_local_elements` where
   * `number_of_local_elements` is the number of collection elements owned by
   * this process that are broadcast to. We fill this vector for all processes
   * that isn't the process we are sending from. Each message has the memory
   * layout: [MessageHeader, target_pid, num_elements, collection_ids, data]
   * At this stage we do not fill in the collection IDs on the target
   * process. We will do that separately.
   *
   * Next we loop over all collection elements and for each one whose ID
   * satisfies the predicate we modify the message vector. For collection
   * elements on remote processes we add the ID to the list of IDs to
   * broadcast to on that remote process (part of the message). For local ones
   * we create a local Invoke message and add it to the vector.
   *
   * Finally, we insert all the local messages into our local message pool and
   * send the remote messages to the corresponding processes.
   *
   * Note that we do not send any messages to processes that have zero
   * collection elements whose ID satisfies the predicate.
   */
  template <class Action, class ParallelComponent, class UnaryPredicate,
            class... Args>
  void broadcast_to(UnaryPredicate&& predicate, Args&&... args);

  /*!
   * \brief Contributes data to a reduction operation on a distributed
   * component.
   *
   * This function is used to perform a reduction across all elements of a
   * parallel component (typically a collection). The reduction callback
   * determines what action is taken when the reduction is complete (e.g.,
   * invoking an action on a specific element or broadcasting to all
   * elements). The callback can be invoked on any parallel component, not
   * just the one the reduction is being done over.
   *
   * \tparam ContributingParallelComponent The parallel component contributing
   *                                       to the reduction.
   * \tparam BinaryOp The stateless binary operator used to combine reduction
   *                  data.
   * \tparam CallbackAction The action to invoke when the reduction is complete.
   * \tparam CallbackParallelComponent The parallel component for the callback.
   * \tparam Args The types of the reduction data arguments.
   *
   * \param reduction_id The unique identifier for this reduction operation.
   * \param reduction_callback The callback to invoke after reduction is
   *                           complete.
   * \param args The reduction data arguments to be combined.
   *
   * \throws Exception if the reduction cannot be performed due to internal
   *         errors, misconfiguration, or because we reached
   *         the maximum configured simultaneous reductions.
   * \note
   * - This function is thread-safe for concurrent calls from multiple threads,
   *   provided each thread uses a unique thread ID.
   * - If fewer elements contribute than expected, the reduction will not
   *   complete, while if more contribute then a race condition will be
   *   incurred and the behavior is completely undefined. The only way to over
   *   contribute is if the same element contributes more than once.
   * - For efficiency there is a maximum number of simultaneous reductions per
   *   parallel component that may occur. This can be controlled in the
   *   constructor of `DistributedTaskDriver`.  If the maximum is reached, an
   *   exception is thrown. In this case you need to increase the number of
   *   allowed simultaneous reductions.
   *
   * \see rts::DistributedTaskDriver::reduction_over()
   */
  template <class ContributingParallelComponent, class BinaryOp,
            class CallbackAction, class CallbackParallelComponent,
            class... Args>
  void reduction(
      std::uint64_t reduction_id,
      reduction::ReductionCallback<CallbackAction, CallbackParallelComponent>
          reduction_callback,
      Args&&... args);

  /*!
   * \brief Contributes data to a reduction operation on a distributed
   * component, with a predicate to select participating elements.
   *
   * This function is used to perform a reduction across a subset of elements of
   * a distributed component (typically a collection), as selected by a
   * user-provided predicate. The reduction callback determines what action is
   * taken when the reduction is complete (e.g., invoking an action on a
   * specific element or broadcasting to all elements). The callback can be
   * invoked on any parallel component, not just the one the reduction is
   * being done over.
   *
   * \tparam ContributingParallelComponent The parallel component contributing
   *                                       to the reduction.
   * \tparam BinaryOp The stateless binary operator used to combine reduction
   *                  data.
   * \tparam CallbackAction The action to invoke when the reduction is complete.
   * \tparam CallbackParallelComponent The parallel component for the callback.
   * \tparam UnaryPredicate The predicate type used to filter which
   *         distributed objects (e.g. elements of a collection) to reduce over.
   * \tparam Args The types of the reduction data arguments.
   *
   * \param predicate A callable that takes a collection index and returns true
   *                  if the element should participate in the reduction.
   * \param reduction_id The unique identifier for this reduction operation.
   * \param reduction_callback The callback to invoke after reduction is
   *                           complete.
   * \param args The reduction data arguments to be combined.
   *
   * \throws Exception if the reduction cannot be performed due to internal
   *         errors, misconfiguration, because the ID of the contributing
   *         component does not satisfy the \p predicate, or because we reached
   *         the maximum configured simultaneous reductions.
   *
   * \note
   * - The predicate must be callable with a collection index of type
   *   `ParallelComponent::rts_collection_index` for a collection parallel
   *   component and with a type `int` for the per-process parallel
   *   component. It must always return a bool.
   * - This function is thread-safe for concurrent calls from multiple threads,
   *   provided each thread uses a unique thread ID.
   * - The expected number of contributions for each reduction ID must be
   *   correctly provided; otherwise the behavior is undefined. If fewer IDs
   *   contribute than expected, the reduction will not complete, while if
   *   more contribute then a race condition will be incurred and the behavior
   *   is completely undefined. The only way to over contribute is if the same
   *   element contributes more than once.
   * - For efficiency there is a maximum number of simultaneous reductions per
   *   parallel component that may occur. This can be controlled in the
   *   constructor of `DistributedTaskDriver`.  If the maximum is reached, an
   *   exception is thrown. In this case you need to increase the number of
   *   allowed simultaneous reductions.
   *
   * \see rts::DistributedTaskDriver::reduction()
   */
  template <class ContributingParallelComponent, class BinaryOp,
            class CallbackAction, class CallbackParallelComponent,
            class UnaryPredicate, class... Args>
  void reduction_over(
      UnaryPredicate&& predicate, std::uint64_t reduction_id,
      reduction::ReductionCallback<CallbackAction, CallbackParallelComponent>
          reduction_callback,
      Args&&... args);

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
  void anchor();

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
   * \brief Serializes the component accounting information for this process.
   *
   * Gathers information about all registered regular and collection
   * components, including their names and, for collections, the indices
   * and process IDs of all elements. The data is serialized into a
   * byte buffer suitable for transmission or comparison between processes.
   *
   * \return A vector of bytes containing the serialized component
   *         accounting data for this process.
   */
  std::vector<char> serialize_component_accounting() const;

  /*!
   * \brief Check that component registration is consistent across all
   * processes.
   *
   * This function serializes the local component accounting information and
   * exchanges it with other processes in the process tree. It compares the
   * registered regular and collection components, as well as collection
   * element indices and their process IDs, to ensure that all processes have
   * registered the same components in the same order.
   *
   * If any inconsistency is found, such as a mismatch in the number, names,
   * or indices of components or collection elements, an exception is thrown.
   *
   * \throws rts::MpiException If any MPI call fails during the consistency
   *         check.
   * \throws rts::Exception If component registration differs across processes.
   */
  void check_component_accounting_consistency() const;

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

  /*!
   * \brief Handles the processing and forwarding of reduction messages.
   *
   * This function processes a reduction message that has been received or
   * generated locally. It updates contribution metadata, combines the message
   * with any existing reduction data, and determines the next step:
   * - If the reduction is complete and this is the root process, it invokes the
   *   callback.
   * - If the reduction is not yet complete, forwards the message to the parent
   *   process or combines it with other contributions as needed.
   *
   * \param in_message The reduction message to process and forward.
   *
   * \throws Exception if the message is invalid or if an internal error occurs.
   */
  void send_reduction_message_impl(Message_t in_message);

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
   * \brief Updates the per-process element count for the current node.
   *
   * A per-process element count is stored in a `std::vector` for each
   * thread. The member variable is
   * `per_process_broadcast_to_number_of_elements_` which is a
   * `vector<vector<int>>`. The outer vector is the size of the number of worker
   * threads plus the number of communication threads (usually we have only 1
   * communication thread). The inner `vector<int>` holds one in for each
   * process and so is of size `number_of_nodes()`. The `int` is the number of
   * collection elements on that process that will receive the broadcast.
   *
   * \tparam ParallelComponent The collection component type.
   * \tparam UnaryPredicate The deduced predicate type.
   * \param predicate Predicate to select elements.
   * \param distributed_object_index Index of the distributed object.
   * \throws Exception if the distributed object is not a collection or on
   * error.
   */
  template <class ParallelComponent, class UnaryPredicate>
  void compute_elements_per_pid(const UnaryPredicate& predicate,
                                std::uint32_t distributed_object_index);

  /*!
   * \brief The type used to store each distributed object or collection.
   *
   * We use a `std::variant` of `std::unique_ptr` so that it is clear if we
   * should be indexing into a collection or not. Essentially, this is used to
   * maximize the chance of catching subtle errors since indexing a magic number
   * in the map is not guaranteed to be safe.
   */
  struct DistributedOjectClassHolder {
    using Map_t = std::unordered_map<uint64_t, CollectionHolder>;

    DistributedOjectClassHolder(
        std::unique_ptr<detail::DistributedObjectBase> in_object,
        std::string in_name, size_t number_of_threads,
        size_t max_simultaneous_reductions);

    DistributedOjectClassHolder(
        std::unordered_map<uint64_t, CollectionHolder> in_objects,
        std::string in_name, size_t number_of_processes,
        size_t number_of_threads, size_t max_simultaneous_reductions);

    using variant_t =
        std::variant<std::unique_ptr<detail::DistributedObjectBase>, Map_t>;
    variant_t objects;
    /// The collection IDs on each process ID. This is completely empty for
    /// non-collection parallel components.
    std::vector<std::vector<std::uint64_t>> ids_per_process{};
    std::string name;
    int number_of_local_objects{-1};
    // We hold a unique_ptr<reduction::Handler> since the Handler itself can
    // be neither copied nor moved, but DistributedOjectClassHolder is stored
    // in a std::vector where move is a necessary feature. Unfortunately this
    // adds a single pointer indirection as overhead. It is possible to get
    // around this if we force users to specify the maximum number of components
    // on construction and then reserve sufficient space (though we'd likely
    // need our own very basic/minimal vector implementation).
    std::unique_ptr<reduction::Handler> reduction_handler{nullptr};
  };

  MPI_Comm rts_comm_{};
  bool finalize_mpi_{false};
  bool mpi_supports_multithreading_{false};
  bool in_insert_mode_{false};
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
  std::vector<detail::ActiveObject> active_object_;

  detail::ParentAndChildren parent_and_children_{};
  int parent_other_child_process_id_{-1};
  std::vector<int> all_left_children_{};
  std::vector<int> all_right_children_{};
  std::vector<int> parent_other_subtree_children_{};
  IncomingMpiMessages_t incoming_mpi_messages_{};
  OutgoingMpiMessages_t outgoing_mpi_messages_{};
  moodycamel::ConcurrentQueue<std::tuple<int, Message_t>> outgoing_messages_{};
  qd::Global global_qd_{};
  static constexpr int local_qd_counts_for_global_qd_ = 50;

  std::vector<std::vector<int>> per_process_broadcast_to_number_of_elements_{};
  // Special values used for different types of messages.
  static constexpr int broadcast_process_id = -1;

  size_t max_simultaneous_reductions_ = 1024;
};

template <class ParallelComponent, class... Args>
void DistributedTaskDriver::insert_parallel_component(Args&&... args) {
  in_insert_mode_ = true;
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
      ParallelComponent::name(), static_cast<size_t>(number_of_threads_ + 1),
      max_simultaneous_reductions_);
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
  if (node_to_insert_on < 0) {
    throw Exception{"The process to insert on must be non-negative but got " +
                    std::to_string(node_to_insert_on)};
  }
  in_insert_mode_ = true;
  static_assert(
      rts::is_collection_v<ParallelComponent>,
      "To insert into a collection use insert_parallel_component_collection");
  static_assert(sizeof(typename ParallelComponent::rts_collection_index) ==
                sizeof(std::uint64_t));
  const std::uint32_t index =
      rts::detail::distributed_object_index<ParallelComponent>();
  using Map =
      std::variant_alternative_t<1, DistributedOjectClassHolder::variant_t>;
  if (index == distributed_objects_.size()) {
    // If we do not have the distributed object collection already inserted,
    // insert it.
    distributed_objects_.emplace_back(Map{}, ParallelComponent::name(),
                                      number_of_nodes(), number_of_threads_ + 1,
                                      max_simultaneous_reductions_);
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

  if (node_to_insert_on >= number_of_nodes_) {
    throw Exception("Cannot insert collection " + ParallelComponent::name() +
                    " into node " + std::to_string(node_to_insert_on) +
                    " because we only have " +
                    std::to_string(number_of_nodes_) + " nodes.\n");
  }

  if (node_to_insert_on == my_node_id_) {
    collection.emplace(std::pair{
        collection_index,
        CollectionHolder{node_to_insert_on,
                         std::unique_ptr<detail::DistributedObjectBase>{
                             std::make_unique<ParallelComponent>(
                                 std::forward<Args>(args)...)}}});
    ++distributed_objects_[index].number_of_local_objects;
  } else {
    // Insert for tracking which node this collection element is on.
    collection.emplace(std::pair{
        collection_index,
        CollectionHolder{
            node_to_insert_on,
            std::unique_ptr<detail::DistributedObjectBase>{nullptr}}});
  }
  distributed_objects_[index]
      .ids_per_process[static_cast<size_t>(node_to_insert_on)]
      .push_back(collection_index);
}

template <class ParallelComponent>
void DistributedTaskDriver::remove_parallel_component_collection(
    const typename ParallelComponent::rts_collection_index& user_index) {
  in_insert_mode_ = true;
  static_assert(
      rts::is_collection_v<ParallelComponent>,
      "To insert into a collection use insert_parallel_component_collection");
  static_assert(sizeof(typename ParallelComponent::rts_collection_index) ==
                sizeof(std::uint64_t));
  const std::uint32_t object_index =
      detail::distributed_object_index<ParallelComponent>();
  if (object_index >= distributed_objects_.size()) {
    throw rts::Exception{"Requested to remove distributed object with index " +
                         std::to_string(object_index) + " and name " +
                         ParallelComponent::name() + " was never inserted."};
  }
  if (distributed_objects_[object_index].objects.index() !=
      detail::Collection) {
    // We should never hit this exception since the static_assert should
    // prevent it. However, an insertion bug could allow it to happen.
    throw Exception{
        "Cannot retrieve the local index from the parallel component " +
        ParallelComponent::name() + " because it is of type " +
        detail::get_output(static_cast<detail::DistributedObjectIndex>(
            distributed_objects_[object_index].objects.index())) +
        " but it should be a collection. In function "
        "remove_parallel_component_collection()"};
  }
  const std::uint64_t collection_index = detail::to_internal(user_index);
  DistributedOjectClassHolder::Map_t& collection =
      std::get<1>(distributed_objects_[object_index].objects);
  if (auto it = collection.find(collection_index); it != collection.end()) {
    if (it->second.process_id == current_node_id()) {
      --distributed_objects_[object_index].number_of_local_objects;
    }
    std::vector<std::uint64_t>& ids_on_pid =
        distributed_objects_[object_index]
            .ids_per_process[static_cast<size_t>(it->second.process_id)];
    ids_on_pid.erase(
        std::find(ids_on_pid.begin(), ids_on_pid.end(), collection_index));
    collection.erase(it);
  } else {
    throw Exception{"Unable to remove collection element " +
                    std::to_string(collection_index) +
                    " from parallel component " + ParallelComponent::name()};
  }
}

template <class ParallelComponent>
auto DistributedTaskDriver::collection_ids_on_processes() const
    -> const std::vector<std::vector<std::uint64_t>>& {
  static_assert(rts::is_collection_v<ParallelComponent>);
  const std::uint32_t object_index =
      detail::distributed_object_index<ParallelComponent>();
  if (object_index >= distributed_objects_.size()) {
    throw rts::Exception{"Requested distributed object with index " +
                         std::to_string(object_index) + " and name " +
                         ParallelComponent::name() + " was never inserted."};
  }
  if (distributed_objects_[object_index].objects.index() !=
      detail::Collection) {
    // We should never hit this exception since the static_assert should
    // prevent it. However, an insertion bug could allow it to happen.
    throw Exception{
        "Cannot retrieve the local index from the parallel component " +
        ParallelComponent::name() + " because it is of type " +
        detail::get_output(static_cast<detail::DistributedObjectIndex>(
            distributed_objects_[object_index].objects.index())) +
        " but it should be a collection. In function "
        "collection_ids_on_processes()."};
  }
  return distributed_objects_[object_index].ids_per_process;
}

template <class ParallelComponent, class Integer>
auto DistributedTaskDriver::collection_ids_on_process(const Integer& pid) const
    -> const std::vector<std::uint64_t>& {
  static_assert(std::is_integral_v<Integer>);
  return collection_ids_on_processes<ParallelComponent>()[static_cast<size_t>(
      pid)];
}

template <class ParallelComponent>
auto DistributedTaskDriver::collection_ids_and_locations() const
    -> const std::unordered_map<std::uint64_t,
                                DistributedTaskDriver::CollectionHolder>& {
  const auto object_index =
      detail::distributed_object_index<ParallelComponent>();
  if (object_index >= distributed_objects_.size()) {
    throw rts::Exception{"Requested distributed object with index " +
                         std::to_string(object_index) + " and name " +
                         ParallelComponent::name() + " was never inserted."};
  }
  if (distributed_objects_[object_index].objects.index() !=
      detail::Collection) {
    // We should never hit this exception since the static_assert should
    // prevent it. However, an insertion bug could allow it to happen.
    throw Exception{
        "Cannot retrieve the local index from the parallel component " +
        ParallelComponent::name() + " because it is of type " +
        detail::get_output(static_cast<detail::DistributedObjectIndex>(
            distributed_objects_[object_index].objects.index())) +
        " but it should be a collection. In function "
        "collection_ids_and_locations()."};
  }
  return std::get<1>(distributed_objects_[object_index].objects);
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

  if (in_insert_mode_) {
    throw Exception{
        "Cannot invoke actions while still in Insert mode. You must first call "
        "driver.insert_barrier() on all processes."};
  }
  if (not thread_pool_->threads_are_active()) {
    throw Exception{"Cannot call invoke() on process " +
                    std::to_string(current_node_id()) +
                    " because the threads have not been launched. You must "
                    "first call driver.launch_threads()."};
  }

  int target_node = -1;
  std::uint64_t collection_index = MessageHeader::no_collection_index();
  if constexpr (rts::is_collection_v<ParallelComponent>) {
    static_assert(
        std::is_same_v<typename ParallelComponent::rts_collection_index,
                       IndexType>);
    const auto object_index =
        rts::detail::distributed_object_index<ParallelComponent>();
    collection_index = detail::to_internal(user_index_or_target_node);
    if (object_index >= distributed_objects_.size()) {
      throw Exception{
          "The parallel component '" + ParallelComponent::name() +
          "' is not known, which means it likely was never inserted."};
    }
    try {
      DistributedOjectClassHolder::Map_t& collection =
          std::get<1>(distributed_objects_[object_index].objects);
      try {
        target_node = collection.at(collection_index).process_id;
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
    send_data(
        target_node,
        rts::create_message(
            threaded_action_relative_ptr<Action, ParallelComponent,
                                         std::decay_t<Args>...>(
                std::make_index_sequence<sizeof...(Args)>{}),
            collection_index,
            detail::distributed_object_index<ParallelComponent>(),
            current_node_id(), target_node, global_qd_.local_sweep_number(),
            false, MessageType::Invoke,
            std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...}));
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
  if (in_insert_mode_) {
    throw Exception{
        "Cannot perform broadcasts while still in Insert mode. You must first "
        "call driver.insert_barrier() on all processes."};
  }
  if (not thread_pool_->threads_are_active()) {
    throw Exception{"Cannot call broadcast() on process " +
                    std::to_string(current_node_id()) +
                    " because the threads have not been launched. You must "
                    "first call driver.launch_threads()."};
  }
  if ((std::is_trivially_copyable_v<std::decay_t<Args>> && ...)) {
    send_data(
        broadcast_process_id,
        rts::create_message(
            threaded_action_relative_ptr<Action, ParallelComponent,
                                         std::decay_t<Args>...>(
                std::make_index_sequence<sizeof...(Args)>{}),
            MessageHeader::no_collection_index(),
            detail::distributed_object_index<ParallelComponent>(),
            current_node_id(),
            // For broadcasts we first set the target process ID to
            // self, then update it as we send to different processes.
            current_node_id(), global_qd_.local_sweep_number(), false,
            MessageType::Broadcast,
            std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...}));
  } else {
    throw Exception{"Serialization in broadcast() is not yet implemented."};
    // send_data(broadcast_process_id, std::move(buffer));
  }
}

template <class Action, class ParallelComponent, class UnaryPredicate,
          class... Args>
void DistributedTaskDriver::broadcast_to(UnaryPredicate&& predicate,
                                         Args&&... args) {
  static_assert(
      is_collection_v<ParallelComponent>,
      "Can only call broadcast_to on a collection parallel component.");
  static_assert(((not(std::is_pointer_v<std::decay_t<Args>> or
                      std::is_array_v<std::decay_t<Args>>)) &&
                 ...),
                "We cannot serialize raw pointers or C-style arrays in a "
                "safe manner. Please wrap these in a container that can "
                "safely handle the serialization.");
  static_assert(
      std::is_invocable_r_v<bool, UnaryPredicate,
                            typename ParallelComponent::rts_collection_index>,
      "Predicate must be callable with collection index and return bool.");
  if (in_insert_mode_) {
    throw Exception{
        "Cannot perform broadcast_to while still in Insert mode. You must "
        "first call driver.insert_barrier() on all processes."};
  }
  if (not thread_pool_->threads_are_active()) {
    throw Exception{"Cannot call broadcast_to() on process " +
                    std::to_string(current_node_id()) +
                    " because the threads have not been launched. You must "
                    "first call driver.launch_threads()."};
  }

  const std::uint32_t distributed_object_index =
      rts::detail::distributed_object_index<ParallelComponent>();
  if (distributed_object_index >= distributed_objects_.size()) {
    throw Exception{
        "Trying to send broadcast_to over an unregistered ParallelComponent, " +
        ParallelComponent::name() + ". Did you forget to insert it?"};
  }

  // Regardless of whether or not we are crossing an address space
  // boundary we cannot store or forward references in the Data, we can
  // only safely store values.
  constexpr bool data_is_trivially_copyable =
      (std::is_trivially_copyable_v<std::decay_t<Args>> && ...);
  using Data_t = std::tuple<std::decay_t<Args>...>;

  // Create one copy of the data that we can then copy into each message to
  // each process.
  std::unique_ptr<std::byte[]> data{nullptr};
  // Always align to the tuple. This may over align but reduces code
  // duplication.
  constexpr size_t data_alignment = alignof(Data_t);
  constexpr size_t data_size = data_is_trivially_copyable
                                   ? sizeof(Data_t)
                                   : std::numeric_limits<size_t>::max();
  if (data_is_trivially_copyable) {
    data = std::unique_ptr<std::byte[]>{new (std::align_val_t{data_alignment})
                                            std::byte[data_size]};
    new (data.get()) Data_t{std::forward<Args>(args)...};
  } else {
    throw Exception{"Serialization in broadcast_to() not yet implemented."};
  }

  // Fill the per_process_broadcast_to_number_of_elements_ vector for this
  // thread.
  compute_elements_per_pid<ParallelComponent>(predicate,
                                              distributed_object_index);

  const std::vector<int>& number_of_elements_per_process =
      per_process_broadcast_to_number_of_elements_[thread_id()];
  if (number_of_elements_per_process.size() !=
      static_cast<size_t>(number_of_nodes())) {
    throw Exception{"The size of number_of_elements_per_process should be " +
                    std::to_string(number_of_nodes()) + " but is " +
                    std::to_string(number_of_elements_per_process.size()) +
                    ". This is an internal bug. Please file an issue with a "
                    "minimal reproducible example."};
  }
  const size_t local_number_of_elements = static_cast<size_t>(
      number_of_elements_per_process[static_cast<size_t>(current_node_id())]);
  std::vector<Message_t> broadcast_to_messages{};
  broadcast_to_messages.reserve(static_cast<size_t>(number_of_nodes()) +
                                local_number_of_elements);

  for (int pid = 0; pid < number_of_nodes(); ++pid) {
    const int number_of_elements_on_pid =
        number_of_elements_per_process[static_cast<size_t>(pid)];
    // If there are no elements to broadcast to on the target process ID then
    // skip.
    if (number_of_elements_on_pid == 0 or pid == current_node_id()) {
      broadcast_to_messages.emplace_back(Message_t{nullptr});
      continue;
    }
    broadcast_to_messages.emplace_back(create_broadcast_to_message(
        threaded_action_relative_ptr<Action, ParallelComponent,
                                     std::decay_t<Args>...>(
            std::make_index_sequence<sizeof...(Args)>{}),
        detail::distributed_object_index<ParallelComponent>(),
        current_node_id(), pid, global_qd_.local_sweep_number(), false,
        number_of_elements_on_pid, data_alignment, data_size,
        reinterpret_cast<Data_t*>(data.get())));
  }
  if (static_cast<size_t>(number_of_nodes()) != broadcast_to_messages.size()) {
    throw Exception{"The number of BroadcastTo messages " +
                    std::to_string(broadcast_to_messages.size()) +
                    " must match the number of nodes " +
                    std::to_string(number_of_nodes())};
  }

  // In order to keep this O(N) we loop over all the collection elements. For
  // collection indices that satisfy the predicate, we add them to the PID
  // that they are on. We use the number of elements tracked by the index
  // stored next to the PID to track how many collection IDs we've added.
  const DistributedOjectClassHolder& distributed_object =
      distributed_objects_[distributed_object_index];
  const DistributedOjectClassHolder::variant_t& objects_variant =
      distributed_object.objects;
  if (objects_variant.index() != detail::Collection) {
    throw Exception{
        "Can only perform a broadcast_to over collections, not " +
        detail::get_output(static_cast<detail::DistributedObjectIndex>(
            objects_variant.index()))};
  }
  const DistributedOjectClassHolder::Map_t& objects =
      std::get<1>(objects_variant);
  for (const auto& [collection_index, collection_holder] : objects) {
    if (predicate(detail::from_internal<ParallelComponent>(collection_index))) {
      if (collection_holder.process_id == current_node_id()) {
        broadcast_to_messages.emplace_back(create_message(
            threaded_action_relative_ptr<Action, ParallelComponent,
                                         std::decay_t<Args>...>(
                std::make_index_sequence<sizeof...(Args)>{}),
            collection_index,
            detail::distributed_object_index<ParallelComponent>(),
            current_node_id(), current_node_id(),
            global_qd_.local_sweep_number(), false, rts::MessageType::Invoke,
            data_alignment, data_size, data.get()));
      } else {
        Message_t& this_message = broadcast_to_messages[static_cast<size_t>(
            collection_holder.process_id)];
        std::uint64_t* const start = reinterpret_cast<std::uint64_t*>(
            std::next(this_message.message.get(), sizeof(MessageHeader)));
        if (*start != static_cast<size_t>(collection_holder.process_id)) {
          throw Exception{
              "Process ID mismatch between the one found in the message: " +
              std::to_string(*start) + " and the one being sent to: " +
              std::to_string(collection_holder.process_id) +
              ". This is an internal bug. Please file an issue with a "
              "minimal reproducible example."};
        }
        const int offset = static_cast<int>(*std::next(start));
        *std::next(start, offset + 2) = collection_index;
        *std::next(start) += 1;
      }
    }
  }

  for (int pid = 0; pid < number_of_nodes(); ++pid) {
    if (pid == current_node_id()) {
      thread_pool_->add_tasks(
          std::make_move_iterator(
              std::next(broadcast_to_messages.begin(), number_of_nodes())),
          local_number_of_elements);
      continue;
    }

    if (broadcast_to_messages[static_cast<size_t>(pid)].message != nullptr) {
      Message_t& this_message = broadcast_to_messages[static_cast<size_t>(pid)];
      if (this_message.get_header()->data_alignment() == 0) {
        throw Exception{"Alignment not set in BroadcastTo."};
      }
      if (this_message.get_header()->message_type() !=
          MessageType::BroadcastTo) {
        throw Exception{"MessageType set incorrectly in BroadcastTo"};
      }
      if (const auto dest_pid =
              this_message.get_header()->destination_process_id();
          dest_pid != pid) {
        throw Exception{"Destination PID is " + std::to_string(dest_pid) +
                        " but should be " + std::to_string(pid)};
      }
      send_data(pid, {std::move(this_message)});
    }
  }
}

template <class ContributingParallelComponent, class BinaryOp,
          class CallbackAction, class CallbackParallelComponent, class... Args>
void DistributedTaskDriver::reduction(
    const std::uint64_t reduction_id,
    reduction::ReductionCallback<CallbackAction, CallbackParallelComponent>
        reduction_callback,
    Args&&... args) {
  reduction_over<ContributingParallelComponent, BinaryOp>(
      reduction::detail::AllElements{}, reduction_id,
      std::move(reduction_callback), std::forward<Args>(args)...);
}

template <class ContributingParallelComponent, class BinaryOp,
          class CallbackAction, class CallbackParallelComponent,
          class UnaryPredicate, class... Args>
void DistributedTaskDriver::reduction_over(
    UnaryPredicate&& predicate, const std::uint64_t reduction_id,
    reduction::ReductionCallback<CallbackAction, CallbackParallelComponent>
        reduction_callback,
    Args&&... args) {
  const std::uint32_t object_index =
      active_object_[thread_id()].distributed_object_index;
  if (object_index >= distributed_objects_.size()) {
    throw Exception{
        "Object index " + std::to_string(object_index) +
        " in reduction is out of range, " +
        std::to_string(distributed_objects_.size()) +
        ". This is an internal bug where either the thread id (" +
        std::to_string(thread_id_) + " number of threads " +
        std::to_string(total_number_of_threads()) +
        ") is out of range, or the active object was not correctly set."};
  }
  DistributedOjectClassHolder& holder = distributed_objects_[object_index];
  if (object_index !=
      rts::detail::distributed_object_index<ContributingParallelComponent>()) {
    throw Exception{
        "The ContributingParallelComponent passed to reduction is " +
        ContributingParallelComponent::name() +
        " but the current component being worked on is " + holder.name};
  }
  if constexpr (not std::is_same_v<std::decay_t<UnaryPredicate>,
                                   reduction::detail::AllElements>) {
    if constexpr (is_collection_v<ContributingParallelComponent>) {
      const auto collection_index =
          detail::from_internal<ContributingParallelComponent>(
              active_object_[thread_id()].target_collection_index);
      if (not predicate(collection_index)) {
        throw Exception{"The collection index (" +
                        detail::get_output(collection_index) +
                        ") contributing to the reduction with ID " +
                        std::to_string(reduction_id) +
                        " does not satisfy the predicate passed to "
                        "reduction_over(). You may only contribute from "
                        "collection elements that satisfy the predicate. The "
                        "reduction is being done on the parallel component " +
                        ContributingParallelComponent::name() + "."};
      }
    } else {
      if (not predicate(current_node_id())) {
        throw Exception{"The current process (" +
                        std::to_string(current_node_id()) +
                        ") contributing to the reduction with ID " +
                        std::to_string(reduction_id) +
                        " does not satisfy the predicate passed to "
                        "reduction_over(). You may only contribute from "
                        "processes that satisfy the predicate. The "
                        "reduction is being done on the parallel component " +
                        ContributingParallelComponent::name() + "."};
      }
    }
  }
  if constexpr (rts::is_collection_v<ContributingParallelComponent>) {
    std::optional<Message_t> message_with_all_local_contributions =
        holder.reduction_handler->insert_or_combine<BinaryOp>(
            [current_pid = current_node_id(), &holder,
             &predicate]() -> std::int32_t {
              if constexpr (std::is_same_v<std::decay_t<UnaryPredicate>,
                                           reduction::detail::AllElements>) {
                (void)predicate;    // Acknowledge we aren't using this.
                (void)current_pid;  // Acknowledge we aren't using this.
                return holder.number_of_local_objects;
              } else {
                const size_t my_pid = static_cast<size_t>(current_pid);
                return std::count_if(
                    holder.ids_per_process[my_pid].begin(),
                    holder.ids_per_process[my_pid].end(),
                    [&predicate](const std::uint64_t index) -> bool {
                      return predicate(
                          detail::from_internal<ContributingParallelComponent>(
                              index));
                    });
              }
            },
            std::is_same_v<std::decay_t<UnaryPredicate>,
                           reduction::detail::AllElements>
                ? MessageType::Reduction
                : MessageType::ReductionOver,
            thread_id_, object_index, reduction_id,
            std::move(reduction_callback), std::forward<Args>(args)...);
    if (not message_with_all_local_contributions.has_value()) {
      return;
    }
    // At this stage we know we have all local contributions to this
    // message. Since the current function could've been called from any
    // thread on the process, we must move the message to the communication
    // thread for further processing. We do this by pushing it to the
    // outgoing_messages_ queue.
    Message_t& message = message_with_all_local_contributions.value();
    // TODO: serialize the data here? We could also store a function pointer to
    // a serialization function.
    holder.reduction_handler
        ->set_interprocess_message_info<ContributingParallelComponent>(
            message, current_node_id(), number_of_nodes(),
            holder.ids_per_process, predicate);
    message.get_header()->member_function_ptr(
        threaded_action_relative_ptr<CallbackAction, CallbackParallelComponent,
                                     std::decay_t<Args>...>(
            std::make_index_sequence<sizeof...(Args)>{}));
    outgoing_messages_.enqueue(std::tuple<int, Message_t>{
        reduction::reduction_process_id, std::move(message)});
  } else {
    // TODO: create the message.
    //
    // holder.reduction_handler
    //     ->set_interprocess_message_info<ContributingParallelComponent>(
    //         message, current_node_id(), number_of_nodes(),
    //         holder.ids_per_process, predicate);
    // message.get_header()->member_function_ptr(
    //     threaded_action_relative_ptr<CallbackAction,
    //     CallbackParallelComponent,
    //                                  std::decay_t<Args>...>(
    //         std::make_index_sequence<sizeof...(Args)>{}));
    throw Exception{
        "Reduction not implemented fully for non-collection components"};
  }
}

template <class ParallelComponent, class UnaryPredicate>
void DistributedTaskDriver::compute_elements_per_pid(
    const UnaryPredicate& predicate,
    const std::uint32_t distributed_object_index) {
  static_assert(is_collection_v<ParallelComponent>,
                "Can only perform a BroadcastTo over a collection.");
  static_assert(
      std::is_invocable_r_v<bool, UnaryPredicate,
                            typename ParallelComponent::rts_collection_index>,
      "Predicate must be callable with collection index and return bool.");

  const DistributedOjectClassHolder& distributed_object =
      distributed_objects_[distributed_object_index];
  const DistributedOjectClassHolder::variant_t& objects_variant =
      distributed_object.objects;
  if (objects_variant.index() != detail::Collection) {
    throw Exception{
        "Can only perform a broadcast_to over collections, not " +
        detail::get_output(static_cast<detail::DistributedObjectIndex>(
            objects_variant.index())) +
        ". Please check that the ParallelComponent is a collection."};
  }
  const DistributedOjectClassHolder::Map_t& objects =
      std::get<1>(objects_variant);

  if (per_process_broadcast_to_number_of_elements_.size() !=
      static_cast<size_t>(number_of_threads_ + 1)) {
    throw Exception{
        "Size of per_process_broadcast_to_pid_offset_info_ must be " +
        std::to_string(number_of_threads_ + 1) + " but is size " +
        std::to_string(per_process_broadcast_to_number_of_elements_.size()) +
        ". This is an internal bug. Please file an issue."};
  }
  // Get the thread-local vector for storing the number of elements on each
  // process that we send to and zero it.
  std::vector<int>& number_of_elements_per_process =
      per_process_broadcast_to_number_of_elements_[thread_id()];
  if (number_of_elements_per_process.size() !=
      static_cast<size_t>(number_of_nodes())) {
    throw Exception{"The size of number_of_elements_per_process (" +
                    std::to_string(number_of_elements_per_process.size()) +
                    ") should match the number of nodes, " +
                    std::to_string(number_of_nodes())};
  }
  number_of_elements_per_process.assign(number_of_elements_per_process.size(),
                                        0);
  // Count the number of elements on each process.
  for (const auto& [collection_index, collection_holder] : objects) {
    if (predicate(detail::from_internal<ParallelComponent>(collection_index))) {
      const auto node_id = static_cast<size_t>(collection_holder.process_id);
      if (node_id >= number_of_elements_per_process.size()) {
        throw Exception{"Node ID " + std::to_string(node_id) +
                        " is out of bounds."};
      }
      ++number_of_elements_per_process[node_id];
    }
  }
}

template <class Action, class ParallelComponent, class... ArgIndexes>
void DistributedTaskDriver::threaded_action_impl(Message_t& message) {
  MessageHeader* header = message.get_header();
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
    DistributedOjectClassHolder::Map_t& distributed_object_collection =
        std::get<1>(
            distributed_objects_[header->distributed_object_index()].objects);
    if (const auto it = distributed_object_collection.find(
            header->target_collection_index());
        it != distributed_object_collection.end()) {
      dynamic_cast<ParallelComponent&>(*it->second.object)
          .template threaded_action<Action>(
              *this,
              detail::from_internal<ParallelComponent>(
                  header->target_collection_index()),
              std::move(std::get<ArgIndexes::index>(*args))...);
    } else {
      throw Exception{
          "Collection index " +
          detail::get_output(detail::from_internal<ParallelComponent>(
              header->target_collection_index())) +
          " or as uint64_t " +
          std::to_string(header->target_collection_index()) +
          " is not in the collection parallel component " +
          ParallelComponent::name()};
    }
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
  auto& objects =
      distributed_task_driver.distributed_objects_[object_index].objects;
  if (objects.index() != detail::DistributedObjectIndex::Regular) {
    // We should never hit this exception since the static_assert should prevent
    // it. However, an insertion bug could allow it to happen.
    throw Exception{
        "Local parallel component expected a Regular component but got " +
        detail::get_output(
            static_cast<detail::DistributedObjectIndex>(objects.index()))};
  }
  return dynamic_cast<ParallelComponent*>(std::get<0>(objects).get());
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
  if (distributed_task_driver.distributed_objects_[object_index]
          .objects.index() != detail::Collection) {
    // We should never hit this exception since the static_assert should prevent
    // it. However, an insertion bug could allow it to happen.
    throw Exception{
        "Cannot retrieve the local index from the parallel component " +
        ParallelComponent::name() + " because it is of type " +
        detail::get_output(static_cast<detail::DistributedObjectIndex>(
            distributed_task_driver.distributed_objects_[object_index]
                .objects.index())) +
        " but it should be a collection. In function "
        "local_parallel_component()."};
  }
  auto& object_collection = std::get<1>(
      distributed_task_driver.distributed_objects_[object_index].objects);
  auto object_it = object_collection.find(collection_index);
  if (object_it == object_collection.end()) {
    return nullptr;
  }
  return dynamic_cast<ParallelComponent*>(object_it->second.object.get());
}

/// \brief Create the DistributedTaskDriver::the_driver object that can be
/// used to globally access the task driver.
DistributedTaskDriver& create_distributed_task_driver(int* argc, char** argv[],
                                                      bool initialize_mpi);
}  // namespace rts
