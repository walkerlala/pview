//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <memory>
#include <string>

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include "mysql_connection.h"

namespace pview {
class MYSQLPS;
class MYSQLConn;
class MYSQLConnMgr;
class MYSQLTableLock;

/******************************************************************************
 * MYSQL connection configuration.
 *
 * Specify how we connect to the backend MYSQL server.
 * Note
 *  - If @socket is not empty, we connect via unix socket,
 *    and ignore host and port.
 *  - If @socket is empty, we connect using TCP host:port
 *  - @password is NOT allowed to be empty.
 ******************************************************************************/
struct MYSQLConnConf {
  std::string host;
  std::string socket;
  int port;
  std::string user;
  std::string password;
  std::string default_schema;
};

/******************************************************************************
 * MYSQL prepared statement wrapper
 *
 * Usually used to
 *  - avoid SQL injection
 *  - reduce overhead of parsing
 *
 * Note:
 *  - All interfaces are NOT thread-safe.
 *  - All interfaces does not throw, and return 0 on success.
 ******************************************************************************/
class MYSQLPS {
 public:
  MYSQLPS(const std::shared_ptr<MYSQLConn> &conn, sql::Connection *mysql_conn,
          const std::string &sql);
  ~MYSQLPS();

  int init();

  void set_int(size_t arg, int64_t val);
  void set_string(size_t arg, const std::string &val);
  int execute();

 protected:
  /**
   * The corresponding MYSQLConn must be alive while the ps is alive,
   * so it is ref here.
   */
  std::shared_ptr<MYSQLConn> conn_;
  sql::Connection *mysql_conn_ = nullptr;
  std::string sql_;
  sql::PreparedStatement *stmt_ = nullptr;
};

/******************************************************************************
 * Represent a single connection to the backend MYSQL server.
 *
 * User should create MYSQLConn using MYSQLConnMgr factory interfaces.
 *
 * Note:
 *  - All interfaces are NOT thread-safe. Parallel task should use different
 *    MYSQLConn object for work.
 *  - All interfaces does not throw, and return 0 on success.
 ******************************************************************************/
class MYSQLConn : public std::enable_shared_from_this<MYSQLConn> {
 public:
  friend class MYSQLConnMgr;
  ~MYSQLConn() = default;

  int init(sql::Driver *mysql_driver);

  int ddl(const std::string &sql) const;
  int dml(const std::string &sql) const;
  /**
   * Multi DML statements within a single transaction, so that they all failed
   * or succeed.
   */
  int multi_dml(const std::vector<std::string> &stmts) const;
  std::unique_ptr<sql::ResultSet> query(const std::string &sql) const;

  std::shared_ptr<MYSQLPS> create_prepare_stmt(const std::string &sql);

 protected:
  MYSQLConn(const MYSQLConnConf &conf) : conf_(conf) {}
  std::unique_ptr<sql::Statement> create_mysql_stmt() const;
  int execute_dml_impl(sql::Statement *stmt, const std::string &sql) const;
  int execute_dml(const std::string &stmt) const;

 protected:
  const MYSQLConnConf conf_;
  std::unique_ptr<sql::Connection> my_conn_ = nullptr;
};

/******************************************************************************
 * Factory class to create & manage MYSQLConn object.
 *
 ******************************************************************************/
class MYSQLConnMgr {
 public:
  MYSQLConnMgr(const MYSQLConnConf &conf) : conf_(conf) {}
  ~MYSQLConnMgr() = default;

  int init();

  std::shared_ptr<MYSQLConn> create_mysql_conn() const;

 protected:
  const MYSQLConnConf conf_;
  sql::Driver *my_driver_ = nullptr;
};

/******************************************************************************
 * RAII-style MYSQL table (WRITE) lock
 *
 ******************************************************************************/
class MYSQLTableLock {
 public:
  MYSQLTableLock(const std::shared_ptr<MYSQLConn> &conn, std::string db_name,
                 std::string table_name)
      : conn_(conn), db_name_(db_name), tbl_name_(table_name), locked_(false) {}
  ~MYSQLTableLock();

  int lock();

 protected:
  std::shared_ptr<MYSQLConn> conn_ = nullptr;
  std::string db_name_;
  std::string tbl_name_;
  bool locked_ = false;
};
}  // namespace pview
