// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Options.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "findus/Exceptions/Exception.hpp"

namespace findus {
std::ostream& operator<<(std::ostream& os, const Options& options) {
  return os << "Options{"
            << "\n  bind_to: " << options.bind_to
            << "\n  task_threads_per_process: "
            << options.task_threads_per_process
            << "\n  comm_thread_do_tasks: " << std::boolalpha
            << options.comm_thread_do_tasks << "\n}";
}

namespace {
std::unordered_map<std::string, std::string> basic_parse_flags(
    const int* argc, char** argv[], const std::vector<std::string>& flags,
    const std::vector<std::string>& boolean_flags) {
  if (argc == nullptr or argv == nullptr or *argv == nullptr) {
    throw Exception("Invalid argc or argv pointer.");
  }

  std::unordered_map<std::string, std::string> result;
  for (int i = 1; i < *argc; ++i) {
    const std::string current((*argv)[i]);
    if (result.contains(current)) {
      throw Exception{"Already parsed flag " + current + " with value " +
                      result.at(current) + "."};
    }
    if (std::find(boolean_flags.begin(), boolean_flags.end(), current) !=
        boolean_flags.end()) {
      result[current] = "TRUE";  // placeholder to be clear about intention.
    }
    if (std::find(flags.begin(), flags.end(), current) == flags.end()) {
      continue;
    }
    if (i + 1 >= *argc) {
      throw Exception("Flag " + current + " is missing a value.");
    }
    result[current] = (*argv)[i + 1];
    ++i;
  }

  for (const auto& flag : flags) {
    if (result.find(flag) == result.end()) {
      throw Exception("Required flag " + flag + " not provided.");
    }
  }

  return result;
}

int to_int(const std::unordered_map<std::string, std::string>& args,
           const std::string& flag) {
  const std::string& text = args.at(flag);
  size_t processed = 0;
  int value = 0;
  try {
    value = std::stoi(text, &processed);
  } catch (const std::invalid_argument&) {
    throw Exception("Failed to parse the value for the flag " + flag +
                    ". Value is not a valid integer: " + text);
  } catch (const std::out_of_range&) {
    throw Exception("Failed to parse the value for the flag " + flag +
                    ". Value is out of int range: " + text);
  }
  if (processed != text.size()) {
    throw Exception("Failed to parse the value for the flag " + flag +
                    ". Trailing characters in integer: " + text);
  }
  return value;
}

BindTo to_bind_to(const std::unordered_map<std::string, std::string>& args,
                  const std::string& flag) {
  const std::string& text = args.at(flag);
  if (text == "Uninitialized") {
    throw Exception("Failed to parse the value for the flag " + flag +
                    ". Binding " + text +
                    " is not a valid binding and is only used to catch bugs "
                    "related to uninitialized state.");
  } else if (text == "None") {
    return BindTo::None;
  } else if (text == "Core") {
    return BindTo::Core;
  } else if (text == "HardwareThread") {
    return BindTo::HardwareThread;
  } else {
    throw Exception("Failed to parse the value for the flag " + flag +
                    ". Unknown binding: " + text);
  }
}
}  // namespace

Options from_command_line(const int* argc, char** argv[]) {
  const auto opts_as_strings = basic_parse_flags(
      argc, argv, {"--findus-task-threads-per-process", "--findus-bind-to"},
      {"--findus-comm-thread-do-tasks"});

  return {to_bind_to(opts_as_strings, "--findus-bind-to"),
          to_int(opts_as_strings, "--findus-task-threads-per-process"),
          opts_as_strings.contains("--findus-comm-thread-do-tasks")};
}
}  // namespace findus

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>

#include "findus/Detail/GetOutput.hpp"

namespace findus {
namespace {
void test_options_stream() {
  using findus::detail::get_output;

  CHECK(
      "Options{\n  bind_to: Uninitialized\n  task_threads_per_process: 0\n  "
      "comm_thread_do_tasks: false\n}" == get_output(Options{}));
  CHECK(
      "Options{\n  bind_to: Core\n  task_threads_per_process: 4\n  "
      "comm_thread_do_tasks: false\n}" ==
      get_output(Options{BindTo::Core, 4, false}));
  CHECK(
      "Options{\n  bind_to: HardwareThread\n  task_threads_per_process: "
      "8\n  comm_thread_do_tasks: false\n}" ==
      get_output(Options{BindTo::HardwareThread, 8, false}));
  CHECK(
      "Options{\n  bind_to: HardwareThread\n  task_threads_per_process: "
      "8\n  comm_thread_do_tasks: true\n}" ==
      get_output(Options{BindTo::HardwareThread, 8, true}));
}

/*!
 * \brief Class to take a series of arguments and convert them into a format
 * that is the same as arguments to `int main(...)`.
 */
class CommandLine {
 public:
  explicit CommandLine(std::initializer_list<const char*> init)
      : argc_(static_cast<int>(init.size())),
        raw_args_(init),
        argv_array_(const_cast<char**>(raw_args_.data())) {}

  const int* argc_ptr() const { return &argc_; }
  char*** argv_ptr() { return &argv_array_; }

 private:
  int argc_;
  std::vector<const char*> raw_args_;
  char** argv_array_;
};

void test_basic_parse_flags() {
  const std::vector<std::string> flags{"--findus-task-threads-per-process",
                                       "--findus-bind-to"};
  const std::vector<std::string> boolean_flags{"--findus-comm-thread-do-tasks"};

  // Successful parsing with exactly one occurrence of each flag.
  CommandLine valid{"prog", "--findus-task-threads-per-process",
                    "4",    "--findus-bind-to",
                    "Core", "--ignored",
                    "noop", "--findus-comm-thread-do-tasks"};
  const auto parsed = basic_parse_flags(valid.argc_ptr(), valid.argv_ptr(),
                                        flags, boolean_flags);
  CHECK(parsed.size() == 3);
  CHECK(parsed.at("--findus-task-threads-per-process") == "4");
  CHECK(parsed.at("--findus-bind-to") == "Core");
  CHECK(parsed.at("--findus-comm-thread-do-tasks") == "TRUE");

  // Duplicate flag should now throw.
  CommandLine duplicated{"prog", "--findus-bind-to", "None", "--findus-bind-to",
                         "Core"};
  CHECK_THROWS_WITH_AS(
      basic_parse_flags(duplicated.argc_ptr(), duplicated.argv_ptr(), flags,
                        boolean_flags),
      "Already parsed flag --findus-bind-to with value None.",
      findus::Exception);

  // Missing trailing value.
  CommandLine missing_value{"prog", "--findus-bind-to"};
  CHECK_THROWS_WITH_AS(
      basic_parse_flags(missing_value.argc_ptr(), missing_value.argv_ptr(),
                        flags, boolean_flags),
      "Flag --findus-bind-to is missing a value.", findus::Exception);

  // Required flag absent.
  CommandLine missing_flag{"prog", "--findus-task-threads-per-process", "2"};
  CHECK_THROWS_WITH_AS(
      basic_parse_flags(missing_flag.argc_ptr(), missing_flag.argv_ptr(), flags,
                        boolean_flags),
      "Required flag --findus-bind-to not provided.", findus::Exception);

  // Null argc pointer.
  CHECK_THROWS_WITH_AS(
      basic_parse_flags(nullptr, valid.argv_ptr(), flags, boolean_flags),
      "Invalid argc or argv pointer.", findus::Exception);

  // Null argv pointer.
  CHECK_THROWS_WITH_AS(
      basic_parse_flags(valid.argc_ptr(), nullptr, flags, boolean_flags),
      "Invalid argc or argv pointer.", findus::Exception);

  // Null *argv pointer.
  char** null_inner = nullptr;
  CHECK_THROWS_WITH_AS(
      basic_parse_flags(valid.argc_ptr(), &null_inner, flags, boolean_flags),
      "Invalid argc or argv pointer.", findus::Exception);
}
}  // namespace

TEST_CASE("Options") {
  test_options_stream();
  test_basic_parse_flags();
}
}  // namespace findus
#endif
