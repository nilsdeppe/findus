
#include <chrono>
#include <limits>
#include <mpi.h>
#include <new>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "Rts/ActionState.hpp"
#include "Rts/Callback.hpp"
// #include "Rts/Detail/ReductionCounter.hpp"
#include "Rts/DistributedObject.hpp"
#include "Rts/DistributedObjectCollection.hpp"
#include "Rts/DistributedTaskDriver.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/ThreadPool.hpp"

template <class...>
struct td;

struct Component0 : public rts::DistributedObject<Component0> {
  Component0() = default;
  ~Component0() override = default;
  static std::string name() { return "Component0"; }

  template <class Action, class... Args>
  void threaded_action(Args&&... args) {
    Action::apply(std::forward<Args>(args)...);
  }
};

struct Action {
  static void apply(rts::DistributedTaskDriver& task_driver, const int t) {
    std::cout << std::string{"Received: " + std::to_string(t) + " on process " +
                             std::to_string(task_driver.current_node_id()) +
                             " on thread " +
                             std::to_string(task_driver.thread_id()) + "\n"};
    // using namespace std::chrono_literals;
    // std::this_thread::sleep_for(30000ms);
    // while (true) {
    // }
  }
};

struct Component1 : public rts::DistributedObjectCollection<Component1> {
  Component1() = default;
  ~Component1() override = default;
  using rts_collection_index = std::uint64_t;

  static std::string name() { return "Component1"; }

  template <class Action, class... Args>
  void threaded_action(rts::DistributedTaskDriver& task_driver,
                       const rts_collection_index& my_index, Args... args) {
    Action::apply(task_driver, my_index, std::forward<Args>(args)...);
  }

  template <>
  void threaded_action<Action>(
      rts::DistributedTaskDriver& task_driver,
      [[maybe_unused]] const rts_collection_index& my_index, const int t) {
    Action::apply(task_driver, t);
  }
};

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
    static void apply(rts::DistributedTaskDriver& task_driver,
                      const std::uint64_t my_index, const std::uint64_t i,
                      const double d) {
      std::cout << std::string{"On " + std::to_string(my_index) + " (" +
                               std::to_string(i) + "," + std::to_string(d) +
                               ")\n"};
    }
  };

  static void apply(rts::DistributedTaskDriver& task_driver,
                    const std::uint64_t my_index) {
    if (task_driver.current_node_id() == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    task_driver.reduction<Component1, MyOp>(
        100,
        Broadcast
            ? rts::reduction::ReductionCallback<PrintResult, Component1>{}
            : rts::reduction::ReductionCallback<PrintResult, Component1>{1},
        my_index, 2.0 * my_index);
  }
};

struct LaunchActions {
  static void apply(rts::DistributedTaskDriver& driver) {
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
  }
};

int main(int argc, char* argv[]) {
  // TODO: I need to understand how core IDs, thread IDs, and MPI ranks should
  // be mapped. Each thread should locally be numbered from 0, with thread 0
  // being the communication thread.
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(&argc, &argv, true);

  driver.insert_parallel_component<Component0>();
  driver.insert_parallel_component_collection<Component1>(
      static_cast<size_t>(1), 0);
  driver.insert_parallel_component_collection<Component1>(
      static_cast<size_t>(5), 0);

  if (driver.number_of_nodes() > 1) {
    // driver.insert_parallel_component_collection<Component1>(
    //     static_cast<size_t>(3), 1);
    // driver.insert_parallel_component_collection<Component1>(
    //     static_cast<size_t>(7), 1);

    driver.insert_parallel_component_collection<Component1>(
        static_cast<size_t>(7), 3);
  }

  driver.insert_barrier();
  driver.launch_threads();

  // b Rts/DistributedTaskDriver.cpp:253

  if (driver.current_node_id() == 0) {
    // Launch actions from an action so that we can delay launch and let the
    // RTS get into a stable state and test for race conditions. I.e. it only
    // works if things are "fast enough". Need to also test that things work
    // if they are "very fast".
    for (int i = 0; i < driver.number_of_nodes(); ++i) {
      driver.invoke<LaunchActions, Component0>(i);
    }
  }
  driver.run_to_quiescence();

  if (driver.current_node_id() == 0) {
    driver.broadcast<StartReduction<true>, Component1>();
  }
  driver.run_to_quiescence();

  driver.force_threads_to_stop();
  return 0;
}
