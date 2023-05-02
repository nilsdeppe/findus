
#include <chrono>
#include <mpi.h>
#include <sstream>

#include "Rts/ThreadPool.hpp"

struct Message {
  static bool execute(rts::ThreadPool<Message, int>& pool, uint32_t thread_id,
                      Message& message, int t) {
    std::stringstream ss;
    ss << "Hello " << thread_id << ' ' << t << ' ' << message.index << "\n";
    pool.print_to(ss.str());

    if (message.index < 10) {
      pool.add_task(thread_id, Message{message.index + 1});
    }
    return true;
  }
  int index{std::numeric_limits<int>::max()};
};

int main(int argc, char *argv[]) {
  MPI_Init(&argc, &argv);

  const size_t number_of_threads = 3;

  rts::ThreadPool<Message, int> thread_pool(number_of_threads, 0, 3);

  for (size_t i = 0; i < 100; ++i) {
    thread_pool.add_task(i % number_of_threads, Message{static_cast<int>(i)});
  }

  thread_pool.launch_threads();

  using namespace std::chrono_literals;
  std::this_thread::sleep_for(2000ms);

  thread_pool.stop();

  MPI_Finalize();
  return 0;
}
