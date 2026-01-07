#include <cstdlib>
#include <iostream>
#include <mpi.h>

int main(int argc, char** argv) {
  std::cout << "Before MPI_Init" << std::endl;
  std::cout << "  PMI_SIZE env: "
            << (std::getenv("PMI_SIZE") ? std::getenv("PMI_SIZE") : "not set")
            << std::endl;
  std::cout << "  PMI_RANK env: "
            << (std::getenv("PMI_RANK") ? std::getenv("PMI_RANK") : "not set")
            << std::endl;

  int provided;
  int err = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  std::cout << "MPI_Init_thread returned: " << err << std::endl;

  int world_size, world_rank;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  std::cout << "After MPI_Init:" << std::endl;
  std::cout << "  Rank: " << world_rank << " / " << world_size << std::endl;

  MPI_Finalize();
  return 0;
}
