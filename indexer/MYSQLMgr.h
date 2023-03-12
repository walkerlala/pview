//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pview {
/******************************************************************************
 * Self-supervised MYSQL database server configuration
 *
 ******************************************************************************/
struct MYSQLDConf {
  std::string mysqld; /* mysqld executable path */
  int port;           /* mysqld listening port  */

  std::string mysqld_tmpdir;  /* mysql database tmpdir (i.e., @@tmpdir) */
  std::string mysqld_logdir;  /* mysql database logging dir */
  std::string mysqld_datadir; /* mysql database data dir (i.e., @@datadir) */
  bool kill_at_exit = false;  /* kill mysqld when the main process exit */
};

/******************************************************************************
 * Running status of the self-supervised MYSQL database server
 *
 ******************************************************************************/
struct MYSQLDStatus {
  bool running = false;
  pid_t mysqld_pid = 0; /* mysqld process id */
};

/******************************************************************************
 * MYSQL database server manager, responsible for spawning/reaping mysqld
 *
 * The pview indexer needs a mysql server for storing indexed result,
 * and the mysql server is either externally provided
 * (i.e., FLAGS_external_mysql == true), or its is spawn locally by MYSQLMgr
 * as child process.
 *
 * If spawn locally as child process, the mysql server will be supervised
 * by the pview process and it is make sure not to live longer than the pview
 * process.
 *
 * Using a external mysql server is good for dev purpose, and using a supervised
 * mysql server is good for production use.
 ******************************************************************************/
class MYSQLMgr {
 public:
  MYSQLMgr(const MYSQLDConf &conf) : inited_(false), conf_(conf) {}
  ~MYSQLMgr();

  bool init();

 protected:
  bool kill_mysqld();
  pid_t start_child_process(const char *cmd, char *const argv[]);
  bool check_command(const char *cmd, char *const argv[]);
  bool check_mysqld_common_args(std::vector<std::string> &args);
  bool check_mysqld_init_args(std::vector<std::string> &args);
  bool check_mysqld_run_args(std::vector<std::string> &args);
  bool create_mysqld_datadir();
  bool init_mysqld_data_dir();
  pid_t exec_mysqld();
  bool start_mysqld();

 protected:
  bool inited_;
  const MYSQLDConf conf_;
  MYSQLDStatus status_;
};
}  // namespace pview
