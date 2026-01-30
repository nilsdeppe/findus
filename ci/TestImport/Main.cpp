
#include <chrono>
#include <cstddef>
#include <limits>
#include <mpi.h>
#include <new>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "findus/ActionState.hpp"
#include "findus/Callback.hpp"
// #include "findus/Detail/ReductionCounter.hpp"
#include "findus/Detail/IndexConversion.hpp"
#include "findus/DistributedObject.hpp"
#include "findus/DistributedObjectCollection.hpp"
#include "findus/DistributedTaskDriver.hpp"
#include "findus/MessageHeader.hpp"
#include "findus/MessageRequeue.hpp"
#include "findus/ThreadPool.hpp"

template <class...>
struct td;

struct Component0 : public findus::DistributedObject<Component0> {
  Component0() = default;
  ~Component0() override = default;
  static std::string name() { return "Component0"; }

  template <class Action, class... Args>
  findus::MessageRequeue findus_invoke_action(Args&&... args) {
    return Action::apply(std::forward<Args>(args)...);
  }
};

struct Action {
  static findus::MessageRequeue apply(
      findus::DistributedTaskDriver& task_driver, const int t) {
    std::cout << std::string{"Received: " + std::to_string(t) + " on process " +
                             std::to_string(task_driver.current_node_id()) +
                             " on thread " +
                             std::to_string(task_driver.thread_id()) + "\n"};
    // using namespace std::chrono_literals;
    // std::this_thread::sleep_for(30000ms);
    // while (true) {
    // }
    return findus::MessageRequeue::Invoked;
  }
};

struct Component1 : public findus::DistributedObjectCollection<Component1> {
  Component1() = default;
  ~Component1() override = default;
  using findus_collection_index = std::uint64_t;

  static std::string name() { return "Component1"; }

  template <class Action, class... Args>
  findus::MessageRequeue findus_invoke_action(
      findus::DistributedTaskDriver& task_driver,
      const findus_collection_index& my_index, Args... args) {
    return Action::apply(task_driver, my_index, std::forward<Args>(args)...);
  }
};

template <>
findus::MessageRequeue Component1::findus_invoke_action<Action>(
    findus::DistributedTaskDriver& task_driver,
    [[maybe_unused]] const findus_collection_index& my_index, const int t) {
  return Action::apply(task_driver, t);
}

template <bool Broadcast>
struct StartReduction {
  struct MyOp {
    void operator()(std::tuple<std::uint64_t, double>& data,
                    const std::uint64_t i, const double d) {
      std::get<0>(data) += i;
      std::get<1>(data) += d;
    }
  };

  struct PrintResult {
    static findus::MessageRequeue apply(
        findus::DistributedTaskDriver& task_driver,
        const std::uint64_t my_index, const std::uint64_t i, const double d) {
      std::cout << std::string{"On " + std::to_string(my_index) + " (" +
                               std::to_string(i) + "," + std::to_string(d) +
                               ")\n"};
      return findus::MessageRequeue::Invoked;
    }
  };

  static findus::MessageRequeue apply(
      findus::DistributedTaskDriver& task_driver,
      const std::uint64_t my_index) {
    if (task_driver.current_node_id() == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    task_driver.reduction<Component1, MyOp>(
        100,
        Broadcast
            ? findus::reduction::ReductionCallback<PrintResult, Component1>{}
            : findus::reduction::ReductionCallback<PrintResult, Component1>{1},
        my_index, 2.0 * my_index);
    return findus::MessageRequeue::Invoked;
  }
};

struct LaunchActions {
  static findus::MessageRequeue apply(findus::DistributedTaskDriver& driver) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (driver.current_node_id() == 1) {
      driver.invoke<Action, Component0>(0, 110);
      driver.broadcast<Action, Component1>(789);
      driver.broadcast<Action, Component0>(456);
      driver.broadcast_to<Action, Component1>(
          [](const std::uint64_t id) { return id == 1; }, 472);
    }
    if (driver.current_node_id() == 0) {
      driver.invoke<Action, Component0>(1, 111);
      driver.broadcast<Action, Component1>(987);
      driver.broadcast<Action, Component0>(654);
      driver.broadcast_to<Action, Component1>(
          [](const std::uint64_t id) { return id == 1; }, 372);
    }
    if (driver.current_node_id() == 1) {
      driver.invoke<Action, Component1>(static_cast<size_t>(1), 123);
      driver.invoke<Action, Component1>(static_cast<size_t>(5), 321);
    }
    return findus::MessageRequeue::Invoked;
  }
};

namespace Parallel::Algorithms {
struct Array {};
struct Nodegroup {};
struct Singleton {};
struct Group {};
}  // namespace Parallel::Algorithms

struct Print {
  template <class ParallelComponent, class IndexType>
  static void apply(const IndexType index, size_t i) {
    std::cout << index << ' ' << i << "\n";
  }
};

template <class Action>
struct SimpleActionWrapper {
  using type = Action;
};

template <class Action>
struct ThreadedActionWrapper {
  using type = Action;
};

template <class Action>
struct ReceiveDataWrapper {
  using type = Action;
};

template <class WrappedAction>
struct IsSimpleAction : std::false_type {};

template <class Action>
struct IsSimpleAction<SimpleActionWrapper<Action>> : std::true_type {};

template <class WrappedAction>
constexpr bool is_simple_action_v = IsSimpleAction<WrappedAction>::value;

template <class WrappedAction>
struct IsThreadedAction : std::false_type {};

template <class Action>
struct IsThreadedAction<ThreadedActionWrapper<Action>> : std::true_type {};

template <class WrappedAction>
constexpr bool is_threaded_action_v = IsThreadedAction<WrappedAction>::value;

template <class WrappedAction>
struct IsReceiveData : std::false_type {};

template <class Action>
struct IsReceiveData<ReceiveDataWrapper<Action>> : std::true_type {};

template <class WrappedAction>
constexpr bool is_receive_data_v = IsReceiveData<WrappedAction>::value;

template <typename ParallelComponent>
struct DistributedObject  // In findus this is the component
    : public std::conditional_t<
          std::is_same_v<typename ParallelComponent::chare_type,
                         Parallel::Algorithms::Array>,
          findus::DistributedObjectCollection<
              DistributedObject<ParallelComponent>>,
          findus::DistributedObject<DistributedObject<ParallelComponent>>> {
  static_assert(std::is_same_v<typename ParallelComponent::chare_type,
                               Parallel::Algorithms::Array> or
                std::is_same_v<typename ParallelComponent::chare_type,
                               Parallel::Algorithms::Nodegroup>);
  using findus_collection_index = typename ParallelComponent::array_index;

  static std::string name() { return ParallelComponent::name(); }

  template <class WrappedAction, class... Args>
  std::enable_if_t<std::is_same_v<typename ParallelComponent::chare_type,
                                  Parallel::Algorithms::Array>,
                   findus::MessageRequeue>
  findus_invoke_action(findus::DistributedTaskDriver& task_driver,
                       const findus_collection_index& my_index,
                       std::tuple<Args...> args) {
    if constexpr (is_simple_action_v<WrappedAction>) {
      // TODO: adjust to spectre
      forward_tuple_to_action(my_index, std::move(args),
                              std::make_index_sequence<sizeof...(Args)>{},
                              WrappedAction{});
    } else if constexpr (is_threaded_action_v<WrappedAction>) {
      // TODO: adjust to spectre
      forward_tuple_to_action(my_index, std::move(args),
                              std::make_index_sequence<sizeof...(Args)>{},
                              WrappedAction{});
    } else {
      throw std::runtime_error{"Help"};
    }
    return findus::MessageRequeue::Invoked;
  }

 private:
  template <template <class> class Wrapper, class Action, class... Args,
            size_t... Is>
  void forward_tuple_to_action(const findus_collection_index& my_index,
                               std::tuple<Args...> args,
                               std::index_sequence<Is...>, Wrapper<Action>) {
    Action::template apply<ParallelComponent>(my_index,
                                              std::move(std::get<Is>(args))...);
  }
};

namespace findus::detail {
DistributedTaskDriver* get_task_driver_ptr();
}  // namespace findus::detail

template <typename ParallelComponent, bool SingleElement = false>
class Proxy {
 public:
  // Implements the current spectre public interface. Sends to a
  // threaded_action overload that selects based on wrapper with enum for
  // action.

  explicit Proxy(std::optional<std::uint64_t> index = std::nullopt)
      : task_driver_on_process_{findus::detail::get_task_driver_ptr()},
        collection_index_(index) {}

  template <class... Args, bool LocalSingleElement = SingleElement>
  std::enable_if_t<std::is_same_v<typename ParallelComponent::chare_type,
                                  Parallel::Algorithms::Array> and
                   LocalSingleElement>
  insert(Args&&... args) {
    static_assert(sizeof...(Args) > 0,
                  "You must pass at least the process on which to insert the "
                  "collection element on.");
    if constexpr (sizeof...(Args) > 0) {
      // Hide in if-constexpr block to improve error messages.
      insert_impl(std::forward_as_tuple(std::forward<Args>(args)...),
                  std::make_index_sequence<sizeof...(Args)>{});
    }
  }

  std::enable_if_t<not std::is_same_v<typename ParallelComponent::chare_type,
                                      Parallel::Algorithms::Singleton>,
                   Proxy<ParallelComponent, true>>
  operator[](const typename ParallelComponent::array_index& index) {
    return Proxy<ParallelComponent, true>{findus::detail::to_internal(index)};
  }

  std::enable_if_t<not std::is_same_v<typename ParallelComponent::chare_type,
                                      Parallel::Algorithms::Singleton>,
                   Proxy<ParallelComponent, true>>
  operator()(const typename ParallelComponent::array_index& index) {
    return Proxy<ParallelComponent, true>{findus::detail::to_internal(index)};
  }

  template <class Action, class... Args>
  void simple_action(std::tuple<Args...> args) {
    if constexpr (SingleElement) {
      if (not collection_index_.has_value()) {
        throw std::runtime_error{"Index is not set in ElementProxy."};
      }
      task_driver_on_process_->invoke<SimpleActionWrapper<Action>,
                                      DistributedObject<ParallelComponent>>(
          findus::detail::from_internal<DistributedObject<ParallelComponent>>(
              *collection_index_),
          std::move(args));
    } else {
      task_driver_on_process_->broadcast<SimpleActionWrapper<Action>,
                                         DistributedObject<ParallelComponent>>(
          std::move(args));
    }
  }

  template <class Action>
  void threaded_action() {}

  // template <class Action, class Arg>
  // void reduction_action(Arg arg);

  // void perform_algorithm();
  // void perform_algorithm(bool restart_if_terminated);

  // // void start_phase(Parallel::Phase next_phase, bool force = false);

  // template <typename ReceiveTag, typename ReceiveDataType>
  // void receive_data(typename ReceiveTag::temporal_id instance,
  //                   ReceiveDataType&& t, bool enable_if_disabled = false);

  // void set_terminate(bool t);

  // void contribute_termination_status_to_main();

 private:
  template <class... Args, size_t... Is>
  void insert_impl(std::tuple<Args...> args,
                   std::index_sequence<Is...> /*meta*/) {
    task_driver_on_process_->insert_parallel_component_collection<
        DistributedObject<ParallelComponent>>(
        findus::detail::get_collection_index<
            DistributedObject<ParallelComponent>>(collection_index_.value()),
        [&args]<size_t I>(std::integral_constant<size_t, I> /*meta*/) {
          if constexpr (I == 0) {
            constexpr bool match_type = std::is_convertible_v<
                std::decay_t<decltype(std::get<sizeof...(Args) - 1>(args))>,
                findus::detail::get_collection_index<
                    DistributedObject<ParallelComponent>>>;
            static_assert(match_type,
                          "The index type passed to insert(..., index) is not "
                          "convertible to the index type of the collection.");
            if constexpr (match_type) {
              return std::get<sizeof...(Args) - 1>(args);
            } else {
              // This path is just for
              throw std::runtime_error{
                  "You should never reach this error. If you do and you did "
                  "not change the code, then please file an issue."};
              std::uint64_t bogus_internal_index{0};
              return findus::detail::from_internal<
                  DistributedObject<ParallelComponent>>(bogus_internal_index);
            }
          } else {
            return std::forward<Args>(std::get<I - 1>(args));
          }
        }(std::integral_constant<size_t, Is>{})...);
  }

  findus::DistributedTaskDriver* task_driver_on_process_{};
  std::optional<std::uint64_t> collection_index_{};
};

struct DgElementArray {
  using chare_type = Parallel::Algorithms::Array;
  using array_index = size_t;
  static std::string name() { return "DgElementArray"; }
};

int main(int argc, char* argv[]) {
  // TODO: I need to understand how core IDs, thread IDs, and MPI ranks should
  // be mapped. Each thread should locally be numbered from 0, with thread 0
  // being the communication thread.
  findus::DistributedTaskDriver& driver =
      findus::create_distributed_task_driver(&argc, &argv,
                                             findus::from_command_line(&argc, &argv));

  Proxy<DgElementArray> array_proxy{};
  Proxy element1_proxy = array_proxy[1];
  element1_proxy.insert(0);
  array_proxy(3).insert(1);

  // driver.insert_parallel_component<Component0>();
  // driver.insert_parallel_component_collection<Component1>(
  //     static_cast<size_t>(1), 0);
  // driver.insert_parallel_component_collection<Component1>(
  //     static_cast<size_t>(5), 0);

  // if (driver.number_of_nodes() > 1) {
  //   // driver.insert_parallel_component_collection<Component1>(
  //   //     static_cast<size_t>(3), 1);
  //   // driver.insert_parallel_component_collection<Component1>(
  //   //     static_cast<size_t>(7), 1);

  //   driver.insert_parallel_component_collection<Component1>(
  //       static_cast<size_t>(7), 3);
  // }

  driver.insert_barrier();
  driver.launch_threads();

  driver.barrier();

  if (driver.current_node_id() == 0) {
    array_proxy.simple_action<Print>(std::tuple{7});
    element1_proxy.simple_action<Print>(std::tuple{8});
  }

  // b Rts/DistributedTaskDriver.cpp:253

  // if (driver.current_node_id() == 0) {
  //   // Launch actions from an action so that we can delay launch and let the
  //   // RTS get into a stable state and test for race conditions. I.e. it only
  //   // works if things are "fast enough". Need to also test that things work
  //   // if they are "very fast".
  //   for (int i = 0; i < driver.number_of_nodes(); ++i) {
  //     driver.invoke<LaunchActions, Component0>(i);
  //   }
  // }
  driver.run_to_quiescence();

  // if (driver.current_node_id() == 0) {
  //   driver.broadcast<StartReduction<true>, Component1>();
  // }
  // driver.run_to_quiescence();

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  driver.force_threads_to_stop();
  return 0;
}
