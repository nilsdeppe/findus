
#include <chrono>
#include <mpi.h>

#include "Rts/ThreadPool.hpp"

int main(int argc, char *argv[]) {
  MPI_Init(&argc, &argv);

  rts::ThreadPool thread_pool(10);

  using namespace std::chrono_literals;
  std::this_thread::sleep_for(2000ms);

  thread_pool.stop();

  MPI_Finalize();
  return 0;
}
