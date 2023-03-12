//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#include <unistd.h>
#include <chrono>
#include <thread>

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <experimental/filesystem>

#include "Common.h"
#include "FileUtils.h"
#include "IndexCtx.h"
#include "IndexQueue.h"
#include "MYSQLConn.h"
#include "PView.h"
#include "ParseTask.h"
#include "SQL.h"
#include "StrUtils.h"
#include "ThreadPool.h"
#include "Timer.h"
#include "Unit.h"
#include "siphash.h"

DEFINE_int64(verbose, 0, "Verbosity. 0 not verbose, 1 verbose, 2 more verbose");
DEFINE_uint64(max_dop, 0, "Max degree of parallism. By default use 80% of CPU");
DEFINE_string(root, "/home/yubin.ryb/projects/P801/",
              "Directory of compile_commands.json");
DEFINE_string(caller_of, "", "Show all caller of this function");
DEFINE_string(callee_of, "",
              "Show all callee of functions; function names separated by ; ");
DEFINE_string(
    callpath, "",
    "Show the any one call-path from start to end. Expected format is "
    "--callpath='start_func;to_func'");
DEFINE_string(
    throwpath, "",
    "Show the callpath to a exception throw beginning from a function");

/** Self-managed mysqld configuration */
DEFINE_string(mysqld, "", "Path of mysqld executable");
DEFINE_string(mysqld_tmpdir, "", "mysqld's --tmpdir argument value");
DEFINE_string(mysqld_logdir, "", "mysqld's log dir (e.g., mysqld.err)");
DEFINE_string(mysqld_datadir, "", "mysqld's --datadir argument value");
DEFINE_bool(kill_mysqld_at_exit, true,
            "Kill managed mysqld when main program exit");

/**
 * MYSQL connection configuration
 */
DEFINE_string(mysqld_host, "127.0.0.1", "Listening host of mysqld");
/** If not empty, connect to mysqld using socket instead of host:port */
DEFINE_string(mysqld_socket, "", "Path of socket of mysqld");
DEFINE_int32(mysqld_port, 8250, "Listening port of mysqld");
DEFINE_string(mysqld_user, "myuser", "User name used to connect to mysqld");
DEFINE_string(mysqld_password, "mypass", "User password");
DEFINE_string(mysqld_default_schema, "", "Default database");

DEFINE_int64(mysql_insert_dead_lock_retry_count, 10,
             "Maximum retry count if mysql insert stmt deadlock");

/**
 * If configured with this, then we connect to an external mysqld to store/load
 * external data, instead of launching our own managed subprocess.
 */
DEFINE_bool(external_mysql, true,
            "Whether or not to use a external mysqld to store indexed data");

/**
 * Batch index mode, where relations between function caller and
 * function callee is not setup while indexing. It is done using
 * SQL join after all tokens are indexed.
 *
 * This used to speedup the first index.
 */
DEFINE_int64(db_update_batch_size, 64,
             ("Num of rows to write into database in a batch. "
              "Very large value might cause the update to fail since it might "
              "exceed the database packet size, or hurt performance"));

/**
 * FLAGS for debug only
 */
DEFINE_string(debug_single_file, "", "Only index this file");

using std::make_shared;
using std::make_unique;
using std::shared_ptr;
using std::unique_ptr;
using std::vector;

using clang::tooling::CompilationDatabase;
using clang::tooling::CompileCommand;

namespace pview {
/** return 0 on success */
int get_mysql_conn_setting(MYSQLConnConf &conf) {
  conf.host = FLAGS_mysqld_host;
  conf.socket = FLAGS_mysqld_socket;
  conf.port = FLAGS_mysqld_port;
  conf.user = FLAGS_mysqld_user;
  conf.password = FLAGS_mysqld_password;
  conf.default_schema = FLAGS_mysqld_default_schema;
  return 0;
}

/** return 0 on success */
int get_mysqld_setting(MYSQLDConf &conf) {
  conf.mysqld = FLAGS_mysqld;
  conf.port = FLAGS_mysqld_port;
  conf.mysqld_tmpdir = FLAGS_mysqld_tmpdir;
  conf.mysqld_logdir = FLAGS_mysqld_logdir;
  conf.mysqld_datadir = FLAGS_mysqld_datadir;
  conf.kill_at_exit = FLAGS_kill_mysqld_at_exit;
  if (conf.mysqld.size() && conf.mysqld_tmpdir.size() &&
      conf.mysqld_logdir.size() && conf.mysqld_datadir.size()) {
    return 0;
  }

  /**
   * Auto-detect using path of current executable
   */
  constexpr size_t kMaxPathLen = 10240;
  char current_exec_path[kMaxPathLen] = {0};

  /** Get dir of the current executable */
  ssize_t sz = readlink("/proc/self/exe", &current_exec_path[0], kMaxPathLen);
  if (sz >= kMaxPathLen || sz <= 0) {
    LOG(ERROR) << "Cannot get path of current executable";
    return 1;
  }
  std::string mypath = std::string(current_exec_path, sz);
  LOG(INFO) << "Current binary path: " << mypath;

  std::string mydir;
  /**
   * Not cross platform, not 100% safe. But we will detect issue afterwards,
   * so it is OK for this.
   */
  auto itr = mypath.rbegin();
  for (; itr < mypath.rend(); itr++) {
    if (*itr == '/') {
      mydir = std::string(mypath.begin(), itr.base());
      break;
    }
  }
  if (!mydir.size()) {
    LOG(ERROR) << "Fail to get dir of current executable"
               << ", cannot determine mysqld path";
    return 1;
  }

  /**
   * Detect mysqld
   */
  if (!conf.mysqld.size()) {
    std::string mysqld_path = mydir + "./mysql/bin/mysqld";
    if (!check_file_executable(mysqld_path)) {
      LOG(ERROR) << "Auto-detected mysqld not a executable: " << mysqld_path;
      LOG(ERROR) << "Use --mysqld=/path/to/mysqld to specify";
      return 1;
    }
    LOG(INFO) << "Found mysqld at: " << mysqld_path;
    conf.mysqld = mysqld_path;
  }

  /**
   * mysqld tmpdir
   */
  if (!conf.mysqld_tmpdir.size()) {
    std::string mysqld_tmpdir = mydir + "./mysql-data/tmpdir";
    if (!check_dir_writable(mysqld_tmpdir)) {
      LOG(ERROR) << "Auto-detected mysqld tmpdir not writable: "
                 << mysqld_tmpdir;
      LOG(ERROR) << "Use --mysqld_tmpdir=/path/to/tmpdir to specify";
      return 1;
    }
    conf.mysqld_tmpdir = mysqld_tmpdir;
  }

  /**
   * mysqld logdir
   */
  if (!conf.mysqld_logdir.size()) {
    std::string mysqld_logdir = mydir + "./mysql-data/logdir";
    if (!check_dir_writable(mysqld_logdir)) {
      LOG(ERROR) << "Auto-detected mysqld logdir not writable: "
                 << mysqld_logdir;
      LOG(ERROR) << "Use --mysqld_logdir=/path/to/logdir to specify";
      return 1;
    }
    conf.mysqld_logdir = mysqld_logdir;
  }

  /**
   * mysqld datadir
   */
  if (!conf.mysqld_datadir.size()) {
    std::string mysqld_datadir = mydir + "./mysql-data/datadir";
    if (!check_dir_writable(mysqld_datadir)) {
      LOG(ERROR) << "Auto-detected mysqld datadir not writable: "
                 << mysqld_datadir;
      LOG(ERROR) << "Use --mysqld_datadir=/path/to/datadir to specify";
      return 1;
    }
    conf.mysqld_datadir = mysqld_datadir;
  }

  {
    LOG(INFO) << "============ mysqld configuration =======";
    LOG(INFO) << "mysqld tmpdir: " << conf.mysqld_tmpdir;
    LOG(INFO) << "mysqld logdir: " << conf.mysqld_logdir;
    LOG(INFO) << "mysqld datadir: " << conf.mysqld_datadir;
    LOG(INFO) << "";
  }
  return 0;
}

/** Entry point of main indexing functionality */
int do_indexing() {
  IndexCtx *ctx = IndexCtx::get_instance();
  CompilationDatabase *cdb = ctx->get_comile_commands();
  ASSERT(cdb);

  vector<CompileCommand> cmds = cdb->getAllCompileCommands();

  size_t num_parse_tasks = ctx->get_num_parse_task();
  num_parse_tasks = std::min(num_parse_tasks, cmds.size());

  const size_t num_store_tasks = ctx->get_num_store_task();
  ASSERT(num_store_tasks >= 1);
  LOG(INFO) << "Num of parse tasks: " << num_parse_tasks;
  LOG(INFO) << "Num of store tasks: " << num_store_tasks;

  auto &thread_pool = ThreadPool::GlobalPool();
  auto counter = make_shared<std::atomic_uint64_t>(0);

  std::vector<shared_ptr<ParseTask>> parse_tasks;
  parse_tasks.reserve(num_parse_tasks);
  for (size_t i = 0; i < num_parse_tasks + num_store_tasks; i++) {
    parse_tasks.emplace_back(make_shared<ParseTask>(std::cref(cmds), counter));
  }

  {
    Timer timer;

    /**
     * Store threads
     */
    result_set<ThreadStat> store_results;
    {
      std::unique_lock<std::mutex> lock(ThreadPool::AddTaskGroupMutex());
      for (size_t i = 0; i < num_store_tasks; i++) {
        size_t idx = i;
        store_results.insert(thread_pool.add_task(idx, &ParseTask::do_store,
                                                  &(*parse_tasks[idx])));
      }
    }

    /**
     * Parse threads
     */
    result_set<ThreadStat> parse_results;
    size_t parse_thread_start_off = num_store_tasks;
    {
      std::unique_lock<std::mutex> lock(ThreadPool::AddTaskGroupMutex());
      for (size_t i = 0; i < num_parse_tasks; i++) {
        size_t idx = parse_thread_start_off + i;
        parse_results.insert(thread_pool.add_task(idx, &ParseTask::do_parse,
                                                  &(*parse_tasks[idx])));
      }
    }

    /** Wait for parse thread finished */
    {
      std::vector<ThreadStat> stats;
      parse_results.get_all_with_except(stats);
      parse_results.clear();
      LOG(INFO) << "Done all indexing in "
                << timer.DurationMicroseconds() / 1000 << " ms.";
    }

    /** Tell store threads to stop after finishing all the stmts. */
    IndexQueue *queue = ctx->get_index_queue();
    queue->set_stop();

    /**
     * If there are too many queued statements, start extra threads to help.
     * Too many threads wouldn't help that much, but might overloaded the
     * backend database server. So 1/2 extra parse thread would be fine.
     */
    if (queue->get_num_stmts() >= 100_KB && (num_parse_tasks / 2) > 0) {
      LOG(INFO) << "Too many SQL stmts in queue. Start "
                << (num_parse_tasks / 2) << " extra store tasks";
      std::unique_lock<std::mutex> lock(ThreadPool::AddTaskGroupMutex());
      for (size_t i = 0; i < num_parse_tasks / 2; i++) {
        size_t idx = parse_thread_start_off + i;
        parse_results.insert(thread_pool.add_task(idx, &ParseTask::do_store,
                                                  &(*parse_tasks[idx])));
      }
    }

    /** Print SQL stmts stats while store threads are still working on it */
    {
      LOG(INFO) << "Waiting for store threads to finish";
      using namespace std::chrono_literals;
      while (queue->get_num_stmts() >= 1_KB) {
        std::this_thread::sleep_for(1s);
        LOG_EVERY_N(INFO, 5) << "num of SQL stmts to be stored into database: "
                             << queue->get_num_stmts();
      }
    }

    /** Join store tasks */
    {
      std::vector<ThreadStat> stats;
      store_results.get_all_with_except(stats);
      store_results.clear();
    }

    /** Join extra join tasks */
    if (parse_results.size()) {
      std::vector<ThreadStat> stats;
      parse_results.get_all_with_except(stats);
      parse_results.clear();
    }

    LOG(INFO) << "Done indexing";
  }
  return 0;
}

/** Query FuncDef USR using qualified function name */
static std::vector<std::string> query_func_def_usr_using_qualified(
    const shared_ptr<MYSQLConn> &mysql_conn, const std::string &qualified) {
  std::vector<std::string> all_caller_usr;
  const auto &stmt = fmt::format(PCTX->get_sql_query_func_def_usr(),
                                 fmt::arg("arg_qualified", qualified));
  auto stmt_result = mysql_conn->query(stmt);
  while (stmt_result->next()) {
    all_caller_usr.push_back(stmt_result->getString(1));
  }
  return all_caller_usr;
}

/** Query FuncCall USR and qualified name using caller's USR */
static void query_func_call_usr_qualified_using_caller_usr(
    const shared_ptr<MYSQLConn> &mysql_conn, std::vector<std::string> &fc_usr,
    std::vector<std::string> &fc_qualified, const std::string &caller_usr) {
  const auto &stmt = fmt::format(PCTX->get_sql_query_func_call_info(),
                                 fmt::arg("arg_caller_usr", caller_usr));
  auto stmt_result = mysql_conn->query(stmt);
  while (stmt_result->next()) {
    fc_usr.push_back(stmt_result->getString(1));
    fc_qualified.push_back(stmt_result->getString(2));
  }
}

/** Query FuncCall USR, qualified, and caller-might-throw using caller's USR */
static void query_func_call_usr_qualified_might_throw_using_caller_usr(
    const shared_ptr<MYSQLConn> &mysql_conn, std::vector<std::string> &fc_usr,
    std::vector<std::string> &fc_qualified,
    std::vector<int> &caller_might_throw, const std::string &caller_usr) {
  const auto &stmt =
      fmt::format(PCTX->get_sql_query_func_call_info_with_throw(),
                  fmt::arg("arg_caller_usr", caller_usr));
  auto stmt_result = mysql_conn->query(stmt);
  while (stmt_result->next()) {
    fc_usr.push_back(stmt_result->getString(1));
    fc_qualified.push_back(stmt_result->getString(2));
    caller_might_throw.push_back(stmt_result->getInt(3));
  }
}

/** Query caller usr from the func_calls table using callee's qualified name */
static void query_func_call_caller_usr_using_qualified(
    const shared_ptr<MYSQLConn> &mysql_conn,
    std::vector<std::string> &caller_usr, const std::string &qualified) {
  const auto &stmt =
      fmt::format(PCTX->get_sql_query_caller_usr_using_qualified(),
                  fmt::arg("arg_qualified", qualified));
  auto stmt_result = mysql_conn->query(stmt);
  while (stmt_result->next()) {
    caller_usr.push_back(stmt_result->getString(1));
  }
}

/**
 * Query func def qualified name from the func_definitions table
 * using func call's USR
 */
static std::string query_func_def_qualified_using_func_def_usr(
    const shared_ptr<MYSQLConn> &mysql_conn, const std::string &func_def_usr) {
  const auto &stmt = fmt::format(PCTX->get_sql_query_func_def_using_qualified(),
                                 fmt::arg("arg_usr", func_def_usr));
  auto stmt_result = mysql_conn->query(stmt);
  while (stmt_result->next()) {
    return stmt_result->getString(1);
  }
  return "";
}

/**
 * Print all callee of function specified via --callee_of=
 */
int do_query_callee_of() {
  ASSERT(FLAGS_callee_of.size());

  IndexCtx *ctx = IndexCtx::get_instance();
  auto *mysql_conn_mgr = ctx->get_mysql_conn_mgr();
  auto mysql_conn = mysql_conn_mgr->create_mysql_conn();
  if (!mysql_conn) {
    LOG(ERROR) << "Fail to create mysql connection. Can't search callee.";
    return 1;
  }

  /** Retrieve USR from qualified name */
  std::vector<std::string> all_caller_usr =
      query_func_def_usr_using_qualified(mysql_conn, FLAGS_callee_of);
  if (!all_caller_usr.size()) {
    LOG(WARNING) << "Cannot find USR using qualified name: " << FLAGS_callee_of;
    return 1;
  }

  /** For every USR, search the func_calls table and print all callee */
  int64_t founded = 0;
  for (size_t n = 0; n < all_caller_usr.size(); n++) {
    const auto &caller_usr = all_caller_usr[n];

    std::vector<std::string> fc_usr;
    std::vector<std::string> fc_qualified;
    query_func_call_usr_qualified_using_caller_usr(mysql_conn, fc_usr,
                                                   fc_qualified, caller_usr);
    ASSERT(fc_usr.size() == fc_qualified.size());
    if (!fc_usr.size()) {
      continue;
    }
    founded++;

    LOG(INFO) << "---------- callee of " << caller_usr << " -----------";
    for (const auto &q : fc_qualified) {
      LOG(INFO) << q;
    }
    LOG(INFO) << "---------- END (" << founded << ") -----------";
  }
  if (founded <= 0) {
    LOG(WARNING) << "Cannot find callee of " << FLAGS_callee_of;
  }
  return 0;
}

/**
 * Print all callee of function specified via --caller_of=
 */
int do_query_caller_of() {
  ASSERT(FLAGS_caller_of.size());

  IndexCtx *ctx = IndexCtx::get_instance();
  auto *mysql_conn_mgr = ctx->get_mysql_conn_mgr();
  auto mysql_conn = mysql_conn_mgr->create_mysql_conn();
  if (!mysql_conn) {
    LOG(ERROR) << "Fail to create mysql connection. Can't search caller.";
    return 1;
  }

  /**
   * Retrieve caller usr from func_calls table using qualified name
   */
  std::vector<std::string> caller_usr;
  query_func_call_caller_usr_using_qualified(mysql_conn, caller_usr,
                                             FLAGS_caller_of);
  if (!caller_usr.size()) {
    LOG(WARNING) << "Cannot find caller using callee qualified name "
                 << FLAGS_caller_of;
    return 1;
  }

  LOG(INFO) << "----------- caller of " << FLAGS_caller_of << "-------------";
  for (const auto &func_def_usr : caller_usr) {
    if (!func_def_usr.size()) {
      LOG(WARNING) << "Invalid empty func_def_usr";
      continue;
    }
    std::string qualified =
        query_func_def_qualified_using_func_def_usr(mysql_conn, func_def_usr);
    if (!qualified.size()) {
      LOG(WARNING) << "Cannot find qualified func def using USR "
                   << func_def_usr
                   << ", maybe the database is not setup correctly";
      continue;
    }
    LOG(INFO) << qualified;
  }
  LOG(INFO) << "----------- END -------------";
  return 0;
}

/** Indentation for tree-based printing */
std::string get_searched_depth_prefix(int level) {
  std::ostringstream oss;
  for (int i = 0; i < level; i++) {
    oss << "    ";
  }
  return oss.str();
}

/** Depth first search of callpath using the `func_calls` table */
bool search_callpath(std::vector<std::string> &result,
                     const shared_ptr<MYSQLConn> &mysql_conn, int level,
                     std::unordered_set<std::string> &searched_caller_usr,
                     const std::vector<std::string> &all_caller_usr,
                     const std::vector<std::string> &all_caller_qualified,
                     const std::string &to_func) {
  ASSERT(all_caller_usr.size() == all_caller_qualified.size());

  for (size_t n = 0; n < all_caller_usr.size(); n++) {
    const auto &caller_usr = all_caller_usr[n];
    const auto &caller_qualified = all_caller_qualified[n];

    LOG_IF(INFO, FLAGS_verbose) << get_searched_depth_prefix(level)
                                << "Searching caller: " << caller_qualified;

    if (!searched_caller_usr.insert(caller_usr).second) {
      LOG_IF(INFO, FLAGS_verbose)
          << get_searched_depth_prefix(level)
          << "Caller already searched: " << caller_qualified;
      continue;
    }

    std::vector<std::string> fc_usr;
    std::vector<std::string> fc_qualified;
    query_func_call_usr_qualified_using_caller_usr(mysql_conn, fc_usr,
                                                   fc_qualified, caller_usr);
    LOG_IF(INFO, FLAGS_verbose)
        << get_searched_depth_prefix(level) << "Num match of caller "
        << caller_qualified << ": " << fc_usr.size();
    for (const auto &fcq : fc_qualified) {
      if (fcq == to_func) {
        result.push_back(to_func);
        return true;
      }
    }

    if (search_callpath(result, mysql_conn, level + 1, searched_caller_usr,
                        fc_usr, fc_qualified, to_func)) {
      result.push_back(caller_qualified);
      return true;
    }
  }
  return false;
}

/** Print any one call path from @start_func to @to_func */
int do_query_callpath() {
  std::vector<std::string> strs = split_string(FLAGS_callpath, ';');
  if (strs.size() != 2 || !strs[0].size() || !strs[1].size()) {
    LOG(ERROR) << "Invalid callpath request format"
               << ", expected --callpath='start_func;to_func'"
               << ", but found: " << FLAGS_callpath;
    return 1;
  }

  const std::string &from_func = strs[0];
  const std::string &to_func = strs[1];
  if (from_func == to_func) {
    LOG(ERROR) << "Duplicate from_func and to_func name. Nothing to do.";
    return 1;
  }

  IndexCtx *ctx = IndexCtx::get_instance();
  auto *mysql_conn_mgr = ctx->get_mysql_conn_mgr();
  auto mysql_conn = mysql_conn_mgr->create_mysql_conn();
  if (!mysql_conn) {
    LOG(ERROR) << "Fail to create mysql connection. Can't search callpath.";
    return 1;
  }

  /** Retrieve USR from qualified name */
  std::vector<std::string> all_caller_usr =
      query_func_def_usr_using_qualified(mysql_conn, from_func);
  if (!all_caller_usr.size()) {
    LOG(WARNING) << "Cannot find USR using qualified name: " << from_func;
    return 1;
  }
  std::vector<std::string> all_caller_qualified(all_caller_usr.size(),
                                                from_func);

  /** Search callpath from @all_caller_usr to @to_func */
  std::unordered_set<std::string> searched_caller_usr;
  std::vector<std::string> result;
  int level = 0;
  search_callpath(result, mysql_conn, level, searched_caller_usr,
                  all_caller_usr, all_caller_qualified, to_func);
  if (!result.size()) {
    LOG(WARNING) << "Cannot find callpath from " << from_func << " to "
                 << to_func;
    return 1;
  }
  std::reverse(result.begin(), result.end());
  LOG(INFO) << "---------- callpath ---------";
  for (const auto &p : result) {
    LOG(INFO) << p;
  }
  LOG(INFO) << "---------- END ---------";
  return 0;
}

/** Depth first search of callpath using the `func_calls` table */
bool search_throwpath(std::vector<std::string> &result,
                      const shared_ptr<MYSQLConn> &mysql_conn, int level,
                      std::unordered_set<std::string> &searched_caller_usr,
                      const std::vector<std::string> &all_caller_usr,
                      const std::vector<std::string> &all_caller_qualified) {
  ASSERT(all_caller_usr.size() == all_caller_qualified.size());

  for (size_t n = 0; n < all_caller_usr.size(); n++) {
    const auto &caller_usr = all_caller_usr[n];
    const auto &caller_qualified = all_caller_qualified[n];

    LOG_IF(INFO, FLAGS_verbose) << get_searched_depth_prefix(level)
                                << "Searching caller: " << caller_qualified;

    if (!searched_caller_usr.insert(caller_usr).second) {
      LOG_IF(INFO, FLAGS_verbose)
          << get_searched_depth_prefix(level)
          << "Caller already searched: " << caller_qualified;
      continue;
    }

    std::vector<std::string> fc_usr;
    std::vector<std::string> fc_qualified;
    std::vector<int> caller_might_throw;
    query_func_call_usr_qualified_might_throw_using_caller_usr(
        mysql_conn, fc_usr, fc_qualified, caller_might_throw, caller_usr);
    ASSERT(fc_usr.size() == fc_qualified.size() &&
           fc_usr.size() == caller_might_throw.size());
    if (!fc_usr.size()) {
      continue;
    }
    int caller_throw = caller_might_throw[0];
    if (caller_throw) {
      result.push_back(caller_qualified);
      return true;
    }

    if (search_throwpath(result, mysql_conn, level + 1, searched_caller_usr,
                         fc_usr, fc_qualified)) {
      result.push_back(caller_qualified);
      return true;
    }
  }
  return false;
}

/**
 * Print any one call path from @throwpath to a function that might throw.
 * @throwpath is usually some destructor.
 */
int do_query_throwpath() {
  ASSERT(FLAGS_throwpath.size());

  const std::string &from_func = FLAGS_throwpath;

  IndexCtx *ctx = IndexCtx::get_instance();
  auto *mysql_conn_mgr = ctx->get_mysql_conn_mgr();
  auto mysql_conn = mysql_conn_mgr->create_mysql_conn();
  if (!mysql_conn) {
    LOG(ERROR) << "Fail to create mysql connection. Can't search throwpath.";
    return 1;
  }

  /** Retrieve USR from qualified name */
  std::vector<std::string> all_caller_usr =
      query_func_def_usr_using_qualified(mysql_conn, from_func);
  if (!all_caller_usr.size()) {
    LOG(WARNING) << "Cannot find USR using qualified name: " << from_func;
    return 1;
  }
  std::vector<std::string> all_caller_qualified(all_caller_usr.size(),
                                                from_func);

  /** Search throwpath from @all_caller_usr */
  std::unordered_set<std::string> searched_caller_usr;
  std::vector<std::string> result;
  int level = 0;
  search_throwpath(result, mysql_conn, level, searched_caller_usr,
                   all_caller_usr, all_caller_qualified);
  if (!result.size()) {
    LOG(WARNING) << "Cannot find throwpath from " << from_func;
    return 1;
  }
  std::reverse(result.begin(), result.end());
  LOG(INFO) << "---------- throwpath ---------";
  for (const auto &p : result) {
    LOG(INFO) << p;
  }
  LOG(INFO) << "---------- END ---------";
  return 0;
}

int do_query() {
  if (FLAGS_callee_of.size()) {
    do_query_callee_of();
  }

  if (FLAGS_caller_of.size()) {
    do_query_caller_of();
  }

  if (FLAGS_callpath.size()) {
    do_query_callpath();
  }

  if (FLAGS_throwpath.size()) {
    do_query_throwpath();
  }

  return 0;
}

int do_prepare() {
  IndexCtx *ctx = IndexCtx::get_instance();
  auto *mysql_conn_mgr = ctx->get_mysql_conn_mgr();
  auto mysql_conn = mysql_conn_mgr->create_mysql_conn();
  if (!mysql_conn) {
    LOG(ERROR) << "Fail to create mysql connection";
    return 1;
  }

  if (mysql_conn->ddl(SQL_create_pview_db)) {
    return 1;
  }

  /** MYSQL table for projects */
  if (mysql_conn->ddl(SQL_projects_create)) {
    return 1;
  }

  /** get associated project id for project root */
  int64_t project_id = 0;
  const std::string &project_root = ctx->get_project_root();
  ASSERT(project_root.size());
  const std::string &query_project_id = fmt::format(
      SQL_get_project_id, fmt::arg("arg_project_root", project_root));
  auto project_id_result = mysql_conn->query(query_project_id);
  while (project_id_result->next()) {
    project_id = project_id_result->getInt(1);
    LOG(INFO) << "Found existing project with id " << project_id;
    break;
  }
  if (project_id <= 0) {
    /**
     * Project not exists in meta table, i.e., new project to index.
     * Assign a new project id.
     */
    auto max_project_id_result = mysql_conn->query(SQL_max_project_id);
    int64_t current_max_project_id = -1;
    while (max_project_id_result->next()) {
      current_max_project_id = max_project_id_result->getInt(1);
      LOG(INFO) << "Current max project id " << current_max_project_id;
      break;
    }
    if (current_max_project_id <= 0) {
      project_id = 1;
    } else {
      project_id = current_max_project_id + 1;
    }

    const std::string &insert_new_project = fmt::format(
        SQL_insert_new_project, fmt::arg("arg_project_id", project_id),
        fmt::arg("arg_project_root", project_root));
    if (mysql_conn->dml(insert_new_project)) {
      LOG(ERROR) << "Failed to insert new project into meta table"
                 << ", project root: " << project_root
                 << ", new project id: " << project_id;
      return 1;
    }
  }
  ctx->set_project_id(project_id);
  ctx->set_project_table_names(fmt::format("filepaths_{}", project_id),
                               fmt::format("func_definitions_{}", project_id),
                               fmt::format("func_calls_{}", project_id));
  LOG(INFO) << "Using project id " << project_id << " for " << project_root;

  /** MYSQL table for filepath */
  if (mysql_conn->ddl(PCTX->get_sql_filepaths_create())) {
    return 1;
  }

  /** MYSQL table for FuncDef */
  if (mysql_conn->ddl(PCTX->get_sql_func_def_create())) {
    return 1;
  }

  /** MYSQL table for FuncCall */
  if (mysql_conn->ddl(PCTX->get_sql_func_call_create())) {
    return 1;
  }

  /** max file path id */
  int64_t max_filepath_id = 1;
  auto max_fn_id_result = mysql_conn->query(PCTX->get_sql_max_filepath_id());
  while (max_fn_id_result->next()) {
    max_filepath_id = max_fn_id_result->getInt(1);
    LOG(INFO) << "Current max file path id " << max_filepath_id
              << ", next will start from " << max_filepath_id + 1;
  }
  ctx->set_next_filepath_id(max_filepath_id + 1);

  /** max func def id */
  int64_t max_func_def_id = 1;
  auto max_func_def_id_result =
      mysql_conn->query(PCTX->get_sql_max_func_def_id());
  while (max_func_def_id_result->next()) {
    max_func_def_id = max_func_def_id_result->getInt(1);
    LOG(INFO) << "Current max func def id " << max_func_def_id
              << ", next will start from " << max_func_def_id + 1;
  }
  ctx->set_next_func_def_id(max_func_def_id + 1);

  /** max func call id */
  int64_t max_func_call_id = 1;
  auto max_func_call_id_result =
      mysql_conn->query(PCTX->get_sql_max_func_call_id());
  while (max_func_call_id_result->next()) {
    max_func_call_id = max_func_call_id_result->getInt(1);
    LOG(INFO) << "Current max func call id: " << max_func_call_id
              << ", next will start from " << max_func_call_id + 1;
  }
  ctx->set_next_func_call_id(max_func_call_id + 1);

  return 0;
}

int do_main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  FLAGS_logtostderr = 1;
  // if (!FLAGS_log_dir.size()) {
  //   FLAGS_log_dir = "/home/yubin.ryb/projects/pview/install/log/";
  // }
  // std::experimental::filesystem::create_directories(FLAGS_log_dir);
  google::InitGoogleLogging(argv[0]);

  IndexCtx *ctx = IndexCtx::get_instance();
  /**
   * Determine actual dop.
   * Unless specified by user, by default we use 80% of the CPU cores.
   *
   * The actual dop is then upper-bounded by thread_pool_size and cpus*3.
   * The minimum dop is 3 (1 parse task and 2 store tasks).
   */
  const size_t cpus = std::thread::hardware_concurrency();
  const size_t default_dop = std::max(1ul, static_cast<size_t>(cpus * 0.8));

  size_t max_dop = FLAGS_max_dop;
  if (!max_dop) {
    max_dop = default_dop;
  }

  /** Upperbound */
  const size_t thread_pool_size = ThreadPool::GlobalPool().size();
  const size_t max_cpus = cpus * 3;
  const size_t cpus_upperbound = std::min(max_cpus, thread_pool_size);
  max_dop = std::min(max_dop, cpus_upperbound);

  /**
   * Minimum cpu is 3 (1 parse task and 2 store tasks).
   * By default 1/8 cpus will be allocated to store tasks, and the rest to parse
   * tasks.
   */
  max_dop = std::max(3ul, max_dop);
  const size_t num_store_tasks = std::max(2ul, max_dop / 8);
  const size_t num_parse_tasks = max_dop - num_store_tasks;
  ctx->set_max_dop(num_parse_tasks, num_store_tasks);

  /**
   * Check compile_commands.json and detect project root
   */
  {
    std::string json_dir = FLAGS_root;
    std::string err_msg;
    unique_ptr<CompilationDatabase> cdb =
        CompilationDatabase::autoDetectFromDirectory(json_dir, err_msg);
    if (err_msg.size() || !cdb) {
      LOG(ERROR) << "Fail to load compilation database from `" << json_dir
                 << "`: " << err_msg;
      return 1;
    }
    if (!cdb->getAllCompileCommands().size()) {
      LOG(ERROR) << "None compile command found in " << FLAGS_root;
      return 1;
    }
    ctx->set_project_root(FLAGS_root);
    ctx->set_compile_commands(std::move(cdb));
    LOG(INFO) << "Project root: " << FLAGS_root;
  }

  /**
   * Spawn our own managed mysqld subprocess if not using external mysqld
   */
  if (!FLAGS_external_mysql) {
    MYSQLDConf mysqld_conf;
    if (get_mysqld_setting(mysqld_conf)) {
      LOG(ERROR) << "Fail to get mysqld setting. Abort.";
      return 1;
    }
    auto mysql_mgr = make_shared<MYSQLMgr>(mysqld_conf);
    if (mysql_mgr->init()) {
      LOG(ERROR) << "Fail to start mysql";
      return 1;
    }
    ctx->set_mysql_mgr(mysql_mgr);
  }

  /**
   * Create mysql connection manager.
   * All subsequent parse task should acquire its own  mysql connection from
   * the manager.
   */
  MYSQLConnConf mysql_conn_conf;
  if (get_mysql_conn_setting(mysql_conn_conf)) {
    LOG(ERROR) << "Fail to get mysql connection setting. Abort.";
    return 1;
  }
  auto mysql_conn_mgr = make_unique<MYSQLConnMgr>(mysql_conn_conf);
  if (mysql_conn_mgr->init()) {
    LOG(ERROR) << "Fail to init mysql connection manage";
    return 1;
  }
  ctx->set_mysql_conn_mgr(std::move(mysql_conn_mgr));

  /**
   * Create index database, tables, setup start id, etc.
   */
  if (do_prepare()) {
    LOG(ERROR) << "Fail to create index database or tables. Abort.";
    return 1;
  }

  /**
   * Setup IndexQueue.
   * Parser put indexed result into IndexQueue, and store thread is responsible
   * for storing these index result into database.
   */
  auto queue = make_unique<IndexQueue>();
  if (queue->init()) {
    LOG(ERROR) << "Fail to init IndexQueue. Abort.";
    return 1;
  }
  ctx->set_index_queue(std::move(queue));

  if (do_indexing()) {
    LOG(ERROR) << "Fail to do indexing. Abort.";
    return 1;
  }

  if (do_query()) {
    LOG(ERROR) << "Fail to answert query. Abort.";
    return 1;
  }

  return 0;
}
}  // namespace pview

int main(int argc, char *argv[]) { return pview::do_main(argc, argv); }
