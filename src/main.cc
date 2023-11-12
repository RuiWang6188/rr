/* -*- Mode: C++; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil; -*- */

#include "main.h"

#include <limits.h>
#include <linux/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <chrono>

#include <sstream>

#include "Command.h"
#include "Flags.h"
#include "RecordCommand.h"
#include "ReplayCommand.h"
#include "core.h"
#include "log.h"
#include "util.h"

using namespace std;

namespace rr {

#if XDEBUG_LATENCY
// Used in calc latency added by RR record
std::chrono::time_point<std::chrono::steady_clock> RR_start;
std::chrono::time_point<std::chrono::steady_clock> tracee_execve;
std::chrono::time_point<std::chrono::steady_clock> start_new_compressed_writer;
std::chrono::time_point<std::chrono::steady_clock> end_new_compressed_writer; 
std::chrono::time_point<std::chrono::steady_clock> tracee_exit;
std::chrono::time_point<std::chrono::steady_clock> RR_exit;

int wait1_counter = 0;
int wait2_counter = 0;
int wait3_counter = 0;
int wait4_counter = 0;
int wait5_counter = 0;
int wait6_counter = 0;

int waitpid1_counter = 0;
int waitpid2_counter = 0;
int waitpid3_counter = 0;
int waitpid4_counter = 0;
int waitpid5_counter = 0;
int waitpid6_counter = 0;
int waitpid7_counter = 0;
int waitpid8_counter = 0;
int waitpid9_counter = 0;
int waitpid10_counter = 0;
#endif

std::chrono::time_point<std::chrono::steady_clock> start_rr;
std::chrono::time_point<std::chrono::steady_clock> end_rr;

std::chrono::time_point<std::chrono::steady_clock> setupenv_start;
std::chrono::time_point<std::chrono::steady_clock> setupenv_end;
std::chrono::time_point<std::chrono::steady_clock> createattach_start;
std::chrono::time_point<std::chrono::steady_clock> createattach_end;
std::chrono::time_point<std::chrono::steady_clock> stopall_start;
std::chrono::time_point<std::chrono::steady_clock> stopall_end;
std::chrono::time_point<std::chrono::steady_clock> scheduling_start;
std::chrono::time_point<std::chrono::steady_clock> scheduling_end;
std::chrono::time_point<std::chrono::steady_clock> patching_start;
std::chrono::time_point<std::chrono::steady_clock> patching_end;
std::chrono::time_point<std::chrono::steady_clock> preload_start;
std::chrono::time_point<std::chrono::steady_clock> preload_end;

std::chrono::time_point<std::chrono::steady_clock> start_execve;
std::chrono::time_point<std::chrono::steady_clock> end_execve;

vector<double> scheduling_time;
vector<double> patching_times;
vector<double> waiting_times;
vector<double> record_event_times;
vector<double> write_frame_times;
vector<double> write_raw_data_times;
vector<double> write_task_event_times;
#if XDEBUG_PATCHING
vector<string> patching_names;
#endif

// Show version and quit.
static bool show_version = false;
static bool show_cmd_list = false;

void assert_prerequisites(bool use_syscall_buffer) {
  struct utsname uname_buf;
  memset(&uname_buf, 0, sizeof(uname_buf));
  if (!uname(&uname_buf)) {
    unsigned int major, minor;
    char dot;
    stringstream stream(uname_buf.release);
    stream >> major >> dot >> minor;
    if (KERNEL_VERSION(major, minor, 0) < KERNEL_VERSION(3, 4, 0)) {
      FATAL() << "Kernel doesn't support necessary ptrace "
              << "functionality; need 3.4.0 or better.";
    }

    if (use_syscall_buffer &&
        KERNEL_VERSION(major, minor, 0) < KERNEL_VERSION(3, 5, 0)) {
      FATAL() << "Your kernel does not support syscall "
              << "filtering; please use the -n option";
    }
  }
}

void print_version(FILE* out) { fprintf(out, "rr version %s\n", RR_VERSION); }

void print_global_options(FILE* out) {
  fputs(
      "Global options:\n"
      "  --disable-cpuid-faulting   disable use of CPUID faulting\n"
      "  --disable-ptrace-exit_events disable use of PTRACE_EVENT_EXIT\n"
      "  --resource-path=PATH       specify the paths that rr should use to "
      "find\n"
      "                             files such as rr_page_*.  These files "
      "should\n"
      "                             be located in PATH/bin, PATH/lib[64], and\n"
      "                             PATH/share as appropriate.\n"
      "  -A, --microarch=<NAME>     force rr to assume it's running on a CPU\n"
      "                             with microarch NAME even if runtime "
      "detection\n"
      "                             says otherwise.  NAME should be a string "
      "like\n"
      "                             'Ivy Bridge'. Note that rr will not work "
      "with\n"
      "                             Intel Merom or Penryn microarchitectures.\n"
      "  -F, --force-things         force rr to do some things that don't "
      "seem\n"
      "                             like good ideas, for example launching an\n"
      "                             interactive emergency debugger if stderr\n"
      "                             isn't a tty.\n"
      "  -E, --fatal-errors         any warning or error that is printed is\n"
      "                             treated as fatal\n"
      "  -M, --mark-stdio           mark stdio writes with [rr <PID> <EV>]\n"
      "                             where EV is the global trace time at\n"
      "                             which the write occurs and PID is the pid\n"
      "                             of the process it occurs in.\n"
      "  -N, --version              print the version number and exit\n"
      "  -S, --suppress-environment-warnings\n"
      "                             suppress warnings about issues in the\n"
      "                             environment that rr has no control over\n"
      "  --log=<spec>               Set logging config to <spec>. See RR_LOG.\n"
      "\n"
      "Environment variables:\n"
      " $RR_LOG        logging configuration ; e.g. RR_LOG=all:warn,Task:debug\n"
      " $RR_TMPDIR     to use a different TMPDIR than the recorded program\n"
      " $_RR_TRACE_DIR where traces will be stored;\n"
      "                falls back to $XDG_DATA_HOME / $HOME/.local/share/rr\n",
      out);
}

void list_commands(FILE* out) {
  Command::print_help_all(out);
}

void print_usage(FILE* out) {
  print_version(out);
  fputs("\nUsage:\n", out);
  list_commands(out);
  fputs("\nIf no subcommand is provided, we check if the first non-option\n"
        "argument is a directory. If it is, we assume the 'replay' subcommand\n"
        "otherwise we assume the 'record' subcommand.\n\n",
        out);
  print_global_options(out);

  /* we should print usage when utility being wrongly used.
     use 'exit' with failure code */
  exit(EXIT_FAILURE);
}

static void init_random() {
  // Not very good, but good enough for our non-security-sensitive needs.
  int key;
  good_random(&key, sizeof(key));
  srandom(key);
  srand(key);
}

bool parse_global_option(std::vector<std::string>& args) {
  static const OptionSpec options[] = {
    { 0, "disable-cpuid-faulting", NO_PARAMETER },
    { 1, "disable-ptrace-exit-events", NO_PARAMETER },
    { 2, "resource-path", HAS_PARAMETER },
    { 3, "log", HAS_PARAMETER },
    { 4, "non-interactive", NO_PARAMETER },
    { 'A', "microarch", HAS_PARAMETER },
    { 'C', "checksum", HAS_PARAMETER },
    { 'D', "dump-on", HAS_PARAMETER },
    { 'E', "fatal-errors", NO_PARAMETER },
    { 'F', "force-things", NO_PARAMETER },
    { 'K', "check-cached-mmaps", NO_PARAMETER },
    { 'L', "list-commands", NO_PARAMETER },
    { 'M', "mark-stdio", NO_PARAMETER },
    { 'N', "version", NO_PARAMETER },
    { 'S', "suppress-environment-warnings", NO_PARAMETER },
    { 'T', "dump-at", HAS_PARAMETER },
  };

  ParsedOption opt;
  if (!Command::parse_option(args, options, &opt)) {
    return false;
  }

  Flags& flags = Flags::get_for_init();
  switch (opt.short_name) {
    case 0:
      flags.disable_cpuid_faulting = true;
      break;
    case 1:
      flags.disable_ptrace_exit_events = true;
      break;
    case 2:
      flags.resource_path = opt.value;
      if (flags.resource_path.back() != '/') {
        flags.resource_path.append("/");
      }
      break;
    case 3:
      apply_log_spec(opt.value.c_str());
      break;
    case 4:
      flags.non_interactive = true;
      break;
    case 'A':
      flags.forced_uarch = opt.value;
      break;
    case 'C':
      if (opt.value == "on-syscalls") {
        LOG(info) << "checksumming on syscall exit";
        flags.checksum = Flags::CHECKSUM_SYSCALL;
      } else if (opt.value == "on-all-events") {
        LOG(info) << "checksumming on all events";
        flags.checksum = Flags::CHECKSUM_ALL;
      } else {
        flags.checksum = strtoll(opt.value.c_str(), NULL, 10);
        LOG(info) << "checksumming on at event " << flags.checksum;
      }
      break;
    case 'D':
      if (opt.value == "RDTSC") {
        flags.dump_on = Flags::DUMP_ON_RDTSC;
      } else {
        flags.dump_on = strtoll(opt.value.c_str(), NULL, 10);
      }
      break;
    case 'E':
      flags.fatal_errors_and_warnings = true;
      break;
    case 'F':
      flags.force_things = true;
      break;
    case 'K':
      flags.check_cached_mmaps = true;
      break;
    case 'M':
      flags.mark_stdio = true;
      break;
    case 'S':
      flags.suppress_environment_warnings = true;
      break;
    case 'T':
      flags.dump_at = strtoll(opt.value.c_str(), NULL, 10);
      break;
    case 'N':
      show_version = true;
      break;
    case 'L':
      show_cmd_list = true;
      break;
    default:
      DEBUG_ASSERT(0 && "Invalid flag");
  }
  return true;
}

static char* saved_argv0_;
static size_t saved_argv0_space_;

char* saved_argv0() {
  return saved_argv0_;
}
size_t saved_argv0_space() {
  return saved_argv0_space_;
}

} // namespace rr

using namespace rr;

int main(int argc, char* argv[]) {
  #if XDEBUG_LATENCY
    RR_start = chrono::steady_clock::now();
  #endif
  #if XDEBUG_WORKFLOW
    start_rr = chrono::steady_clock::now();
  #endif
  setupenv_start = chrono::steady_clock::now();
  auto main_start = chrono::steady_clock::now();
  rr::saved_argv0_ = argv[0];
  rr::saved_argv0_space_ = argv[argc - 1] + strlen(argv[argc - 1]) + 1 - rr::saved_argv0_;

  init_random();
  auto after_init_rand = chrono::steady_clock::now();
  #if XDEBUG
    cout << "[main] init_random: " << chrono::duration <double, milli> (after_init_rand - main_start).count() << " ms" << endl;
  #endif
  raise_resource_limits();
  auto after_raise_resource_limits = chrono::steady_clock::now();
  #if XDEBUG
    cout << "[main] raise_resource_limits: " << chrono::duration <double, milli> (after_raise_resource_limits - after_init_rand).count() << " ms" << endl;
  #endif
  vector<string> args;
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }

  while (parse_global_option(args)) {
  }
  auto after_parse_global_option = chrono::steady_clock::now();
  #if XDEBUG
    cout << "[main] parse_global_option: " << chrono::duration <double, milli> (after_parse_global_option - after_raise_resource_limits).count() << " ms" << endl;
  #endif

  if (show_version) {
    print_version(stdout);
    return 0;
  }
  if (show_cmd_list) {
    list_commands(stdout);
    return 0;
  }

  if (args.size() == 0) {
    print_usage(stderr);
  }

  auto command = Command::command_for_name(args[0]);
  if (command) {
    args.erase(args.begin());
  } else {
    if (!Command::verify_not_option(args)) {
      print_usage(stderr);
    }
    auto before_get_cmd = chrono::steady_clock::now();
    if (is_directory(args[0].c_str())) {
      command = ReplayCommand::get();
    } else {
      command = RecordCommand::get();
    }
    auto after_get_cmd = chrono::steady_clock::now();
    #if XDEBUG
      cout << "[main] get record cmd: " << chrono::duration <double, milli> (after_get_cmd - before_get_cmd).count() << " ms" << endl;
    #endif
  }
  auto before_cmd_run = chrono::steady_clock::now();
  int res = command->run(args);
  auto after_cmd_run = chrono::steady_clock::now();
  #if XDEBUG
    cout << "[main] record_cmd::run: " << chrono::duration <double, milli> (after_cmd_run - before_cmd_run).count() << " ms" << endl;
  #endif

  #if XDEBUG_WORKFLOW
  end_rr = chrono::steady_clock::now();
  cout << "rr total time: " << chrono::duration <double, milli> (end_rr - start_rr).count() << " ms" << endl;
  cout << "from end execve to end rr: " << chrono::duration <double, milli> (end_rr - end_execve).count() << " ms" << endl;
  #endif 
  #if XDEBUG_LATENCY
    RR_exit = chrono::steady_clock::now();
    cout << "tracee exit - RR exit: " << chrono::duration <double, milli> (RR_exit - tracee_exit).count() << " ms" << endl;
  
    cout << "wait() call times distribution:" << endl;
    cout << "\twait 1: " << wait1_counter << endl;
    cout << "\twait 2: " << wait2_counter << endl;
    cout << "\twait 3: " << rr::wait3_counter << endl;
    cout << "\twait 4: " << rr::wait4_counter << endl;
    cout << "\twait 5: " << wait5_counter << endl;
    cout << "\twait 6: " << wait6_counter << endl;
  
    cout << "waitpid() call times distribution:" << endl;
    cout << "\twaitpid 1: " << waitpid1_counter << endl;
    cout << "\twaitpid 2: " << waitpid2_counter << endl;
    cout << "\twaitpid 3: " << waitpid3_counter << endl;
    cout << "\twaitpid 4: " << waitpid4_counter << endl;
    cout << "\twaitpid 5: " << waitpid5_counter << endl;
    cout << "\twaitpid 6: " << waitpid6_counter << endl;
    cout << "\twaitpid 7: " << waitpid7_counter << endl;
    cout << "\twaitpid 8: " << waitpid8_counter << endl;
    cout << "\twaitpid 9: " << waitpid9_counter << endl;
    cout << "\twaitpid 10: " << waitpid10_counter << endl;
  #endif

  return res;
}
