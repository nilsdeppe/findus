#include <iostream>

int main() {
#if defined(__GNUC__) || defined(__clang__)
#ifdef __x86_64__
  std::cout << "x86-64 (GCC/Clang)\n";
#elif __i386__
  std::cout << "x86-32 (GCC/Clang)\n";
#elif __aarch64__
  std::cout << "ARM-64 (GCC/Clang)\n";
#elif __arm__
  std::cout << "ARM-32 (GCC/Clang)\n";
#elif __riscv
#if __riscv_xlen == 64
  std::cout << "RISC-V 64 (GCC/Clang)\n";
#else
  std::cout << "RISC-V 32 (GCC/Clang)\n";
#endif
#else
  std::cout << "Unknown (GCC/Clang)\n";
#endif

#elif defined(_MSC_VER)
#ifdef _M_X64
  std::cout << "x86-64 (MSVC)\n";
#elif _M_IX86
  std::cout << "x86-32 (MSVC)\n";
#elif _M_ARM64
  std::cout << "ARM-64 (MSVC)\n";
#elif _M_ARM
  std::cout << "ARM-32 (MSVC)\n";
#else
  std::cout << "Unknown (MSVC)\n";
#endif

#else
  std::cout << "Unknown (Unsupported compiler)\n";
#endif

  return 0;
}
