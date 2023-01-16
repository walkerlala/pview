#include <signal.h>   /* SIGKILL */
#include <sys/wait.h> /* waitpid() */
#include <unistd.h>
#include <experimental/filesystem>
#include <sstream>

#include <glog/logging.h>

#include "Common.h"
#include "FileUtils.h"
#include "MYSQLMgr.h"

extern char **environ;

namespace pview {
constexpr size_t kMaxNumArgs = 128;

bool MYSQLMgr::init() {
  bool error = start_mysqld();
  if (error) {
    LOG(ERROR) << "MYSQLMgr fail to start mysqld";
    return error;
  }
  return false;
}

MYSQLMgr::~MYSQLMgr() {
  if (conf_.kill_at_exit) {
    kill_mysqld();
  }
}

bool MYSQLMgr::kill_mysqld() {
  if (status_.running && status_.mysqld_pid > 0) {
    LOG(INFO) << "MYSQLMgr exit. Killing mysql...";
    int res = kill(status_.mysqld_pid, SIGKILL);
    if (res) {
      LOG(ERROR) << "Fail to kill child mysqld, errno: " << errno;
      return true;
    }
  }
  return true;
}

/**
 * Start child process.
 *
 * return -1 if start failed.
 */
pid_t MYSQLMgr::start_child_process(const char *cmd, char *const argv[]) {
  pid_t child_pid = fork();
  if (child_pid == -1) {
    LOG(ERROR) << "Fail to fork, errno=" << errno;
    return -1;
  }

  if (child_pid == 0) { /* child. execve immediately */
    execve(cmd, argv, environ);
  }
  return child_pid;
}

/** Execute command and check that return value is 0 */
bool MYSQLMgr::check_command(const char *cmd, char *const argv[]) {
  ASSERT(cmd);

  pid_t child_pid = start_child_process(cmd, argv);
  if (child_pid <= 0) {
    LOG(ERROR) << "Failed to start cmd: " << cmd;
    return true;
  }

  pid_t w = 0;
  int wstatus = 0;
  do {
    w = waitpid(child_pid, &wstatus, WUNTRACED | WCONTINUED);
    if (w == -1) {
      LOG(ERROR) << "Fail to waitpid: " << child_pid;
      return true;
    }

    if (WIFEXITED(wstatus)) {
      int status = WEXITSTATUS(wstatus);
      LOG(INFO) << "Executed cmd: " << cmd << ", status=" << status;
      return (status != 0); /* done */
    } else if (WIFSIGNALED(wstatus)) {
      int sig = WTERMSIG(wstatus);
      LOG(INFO) << "cmd: " << cmd << ", killed by signal: " << sig;
      return true; /* killed, return error */
    } else if (WIFSTOPPED(wstatus)) {
      int sig = WSTOPSIG(wstatus);
      LOG(WARNING) << "cmd: " << cmd << ", stopped by signal: " << sig;
      continue; /* continue */
    } else if (WIFCONTINUED(wstatus)) {
      LOG(WARNING) << "cmd: " << cmd << ", continued.";
      continue; /* continue */
    } else {
      break;
    }
  } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));

  LOG(ERROR) << "cmd: " << cmd << ", waitpid return by unknown status.";
  return true;
}

/** Common mysqld intialize / run args */
bool MYSQLMgr::check_mysqld_common_args(std::vector<std::string> &args) {
  /**
   * --log-error=
   */
  std::string mysqld_logdir = conf_.mysqld_logdir;
  if (!mysqld_logdir.size()) {
    LOG(ERROR) << "Empty mysqld logdir";
    return true;
  } else if (!check_dir_writable(mysqld_logdir)) {
    LOG(ERROR) << "mysqld logdir not writable: " << mysqld_logdir;
    return true;
  }
  std::string mysqld_log_file = mysqld_logdir + "/mysqld.err";
  if (!check_file_writable(mysqld_log_file)) {
    LOG(ERROR) << "mysqld log file not writable: " << mysqld_log_file;
    return true;
  }
  std::string arg_log_error = "--log-error=" + mysqld_log_file;
  args.push_back(arg_log_error);

  std::string mysqld_datadir = conf_.mysqld_datadir;
  if (!mysqld_datadir.size()) {
    LOG(ERROR) << "Empty mysqld datadir";
    return true;
  }
  if (!check_dir_writable(mysqld_datadir)) {
    LOG(ERROR) << "mysqld datadir not writable: " << mysqld_datadir;
    return true;
  }
  args.push_back("--datadir=" + mysqld_datadir);
  args.push_back("--innodb_data_home_dir=" + mysqld_datadir);
  args.push_back("--innodb_undo_directory=" + mysqld_datadir);
  args.push_back("--innodb_log_group_home_dir=" + mysqld_datadir);
  return false;
}

/** Check mysqld intialize args and return usable args vector */
bool MYSQLMgr::check_mysqld_init_args(std::vector<std::string> &args) {
  if (check_mysqld_common_args(args)) {
    return true;
  }
  args.push_back("--initialize-insecure");

  {
    std::ostringstream oss;
    oss << conf_.mysqld.c_str();
    for (const auto &arg : args) {
      oss << " " << arg;
    }
    LOG(INFO) << "mysqld initialize cmdline: " << oss.str();
  }

  return false;
}

/** Check mysqld running args and return usable args vector */
bool MYSQLMgr::check_mysqld_run_args(std::vector<std::string> &args) {
  if (check_mysqld_common_args(args)) {
    return true;
  }
  /**
   * --tmpdir=
   * --socket=
   *  --pid-file=
   */
  std::string tmpdir = conf_.mysqld_tmpdir;
  std::string mysql_sock_file = tmpdir + "/mysql.sock";
  std::string mysql_pid_file = tmpdir + "/mysql.pid";
  if (!tmpdir.size()) {
    LOG(ERROR) << "Empty mysqld tmpdir";
    return true;
  }
  if (!check_dir_writable(tmpdir)) {
    LOG(ERROR) << "mysql tmpdir not writable: " << tmpdir;
    return true;
  }
  std::string arg_tmpdir = "--tmpdir=" + tmpdir;
  std::string arg_socket = "--socket=" + mysql_sock_file;
  std::string arg_pid = "--pid-file=" + mysql_pid_file;
  args.push_back(arg_tmpdir);
  args.push_back(arg_socket);
  args.push_back(arg_pid);

  /**
   * --port=
   */
  int port = conf_.port;
  if (port <= 1024) {
    LOG(ERROR) << "Invalid mysqld port: " << port
               << ", must be larger than 1024";
    return true;
  } else if (check_port_in_use(port)) {
    LOG(ERROR) << "mysqld port not free: " << port;
    return true;
  }
  std::string arg_port = "--port=" + std::to_string(port);
  args.push_back(arg_port);

  {
    std::ostringstream oss;
    oss << conf_.mysqld.c_str();
    for (const auto &arg : args) {
      oss << " " << arg;
    }
    LOG(INFO) << "mysqld run cmdline: " << oss.str();
  }

  return false;
}

/** create datadir if not existed */
bool MYSQLMgr::create_mysqld_datadir() {
  if (!conf_.mysqld_tmpdir.size()) {
    LOG(ERROR) << "Empty tmp directory. Please specify mysqld tmpdir using "
                  "--mysqld_tmpdir";
    return true;
  }
  std::experimental::filesystem::create_directories(conf_.mysqld_tmpdir);
  if (!check_dir_writable(conf_.mysqld_tmpdir)) {
    LOG(ERROR) << "Fail to create directory: " << conf_.mysqld_tmpdir;
    return true;
  }

  if (!conf_.mysqld_logdir.size()) {
    LOG(ERROR) << "Empty log directory. Please specify mysqld logdir using "
                  "--mysqld_logdir";
    return true;
  }
  std::experimental::filesystem::create_directories(conf_.mysqld_logdir);
  if (!check_dir_writable(conf_.mysqld_logdir)) {
    LOG(ERROR) << "Fail to create directory: " << conf_.mysqld_logdir;
    return true;
  }

  if (!conf_.mysqld_datadir.size()) {
    LOG(ERROR) << "Empty data directory. Please specify mysqld datadir using "
                  "--mysqld_datadir";
    return true;
  }
  std::experimental::filesystem::create_directories(conf_.mysqld_datadir);
  if (!check_dir_writable(conf_.mysqld_datadir)) {
    LOG(ERROR) << "Fail to create directory: " << conf_.mysqld_datadir;
    return true;
  }
  LOG(INFO) << "mysqld data directory created";
  return false;
}

bool MYSQLMgr::init_mysqld_data_dir() {
  /** Existing datadir implies that it is already inited, so skip */
  if (!std::experimental::filesystem::is_empty(conf_.mysqld_datadir)) {
    LOG(INFO) << "data dir " << conf_.mysqld_datadir
              << " not empty. Skip initialize";
    return false;
  }

  /** create datadir if not existed */
  if (create_mysqld_datadir()) {
    LOG(ERROR) << "Fail to create mysqld datadir";
    return true;
  }

  std::vector<std::string> args;
  if (check_mysqld_init_args(args)) {
    return true;
  }
  if (args.size() >= kMaxNumArgs) {
    LOG(ERROR) << "Too many args: " << args.size() << " (max: " << kMaxNumArgs
               << ")";
    return true;
  }
  char *argv[kMaxNumArgs] = {NULL};
  size_t off = 0;

  argv[off++] = const_cast<char *>(conf_.mysqld.c_str());
  for (size_t i = 0; i < kMaxNumArgs - 1 && i < args.size(); i++) {
    argv[off++] = const_cast<char *>(args[i].c_str());
  }
  bool res = check_command(conf_.mysqld.c_str(), argv);
  return res;
}

pid_t MYSQLMgr::exec_mysqld() {
  std::vector<std::string> args;
  if (check_mysqld_run_args(args)) {
    return -1;
  }
  if (args.size() >= kMaxNumArgs) {
    LOG(ERROR) << "Too many args: " << args.size() << " (max: " << kMaxNumArgs
               << ")";
    return -1;
  }

  char *argv[kMaxNumArgs] = {NULL};
  size_t off = 0;

  argv[off++] = const_cast<char *>(conf_.mysqld.c_str());
  for (size_t i = 0; i < kMaxNumArgs - 1 && i < args.size(); i++) {
    argv[off++] = const_cast<char *>(args[i].c_str());
  }

  return start_child_process(conf_.mysqld.c_str(), argv);
}

bool MYSQLMgr::start_mysqld() {
  if (status_.running) {
    LOG(WARNING) << "mysqld running flag already set"
                 << ", pid=" << status_.mysqld_pid
                 << ", configured port=" << conf_.port;
    /** Might be false positive, but it is OK */
    if (check_port_in_use(conf_.port)) {
      return false;
    }
  }

  /** initialize mysqld data dir */
  if (init_mysqld_data_dir()) {
    LOG(ERROR) << "Fail to initialize mysqld datadir";
    return true;
  }

  /** spawn mysqld process */
  pid_t child_pid = exec_mysqld();
  if (child_pid <= 0) {
    LOG(ERROR) << "Fail to lauch mysqld as child process";
    return true;
  }
  LOG(INFO) << "Started mysqld with pid=" << child_pid;

  status_.running = true;
  status_.mysqld_pid = child_pid;
  return false;
}
}  // namespace pview
