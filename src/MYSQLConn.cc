#include "MYSQLConn.h"
#include "Common.h"
#include "FileUtils.h"

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

using std::make_shared;
using std::make_unique;
using std::shared_ptr;
using std::unique_ptr;

DECLARE_int64(mysql_insert_dead_lock_retry_count);

namespace pview {
static void get_mysql_exception_msg(std::ostringstream &oss,
                                    sql::SQLException &e, const char *file,
                                    const char *func_name, int64_t line) {
  /* clang-format off */
  oss << "# ERR: SQLException in " << file
      << "(" << func_name << ") on line " << line
      << ", ERR: " << e.what()
      << " (MySQL error code: " << e.getErrorCode()
      << ", SQLState: " << e.getSQLState()
      << ")";
  /* clang-format on */
}

static std::string get_mysql_exception_msg(sql::SQLException &e,
                                           const char *file,
                                           const char *func_name,
                                           int64_t line) {
  std::ostringstream oss;
  get_mysql_exception_msg(oss, e, file, func_name, line);
  return oss.str();
}

/******************************************************************************
 * MYSQLPS
 *
 ******************************************************************************/
MYSQLPS::MYSQLPS(const shared_ptr<MYSQLConn> &conn, sql::Connection *mysql_conn,
                 const std::string &sql)
    : conn_(conn), mysql_conn_(mysql_conn), sql_(sql) {}

MYSQLPS::~MYSQLPS() { delete stmt_; }

int MYSQLPS::init() {
  try {
    stmt_ = mysql_conn_->prepareStatement(sql_);
  } catch (sql::SQLException &e) {
    std::ostringstream oss;
    get_mysql_exception_msg(oss, e, __FILE__, __FUNCTION__, __LINE__);
    oss << ", sql: " << sql_;
    LOG(ERROR) << oss.str();
    return 1;
  } catch (...) {
    ASSERT(false);
    return 1;
  }
  ASSERT(stmt_);
  return 0;
}

void MYSQLPS::set_int(size_t arg, int64_t val) {
  ASSERT(stmt_);
  try {
    stmt_->setInt(arg, val);
  } catch (sql::SQLException &e) {
    LOG(ERROR) << get_mysql_exception_msg(e, __FILE__, __FUNCTION__, __LINE__);
  } catch (...) {
    ASSERT(false);
  }
}

void MYSQLPS::set_string(size_t arg, const std::string &val) {
  ASSERT(stmt_);
  try {
    stmt_->setString(arg, val);
  } catch (sql::SQLException &e) {
    LOG(ERROR) << get_mysql_exception_msg(e, __FILE__, __FUNCTION__, __LINE__);
  } catch (...) {
    ASSERT(false);
  }
}

int MYSQLPS::execute() {
  ASSERT(stmt_);

  size_t cnt = 1;
  for (; cnt < FLAGS_mysql_insert_dead_lock_retry_count; ++cnt) {
    try {
      stmt_->execute();
    } catch (sql::SQLException &e) {
      bool is_deadlock = (e.getErrorCode() == 1213);
      bool is_lock_wait_timeout = (e.getErrorCode() == 1205);
      bool is_mysql_gone_away = (e.getErrorCode() == 2006);
      bool is_recoverable =
          (is_deadlock || is_lock_wait_timeout || is_mysql_gone_away);
      if (is_recoverable && cnt < FLAGS_mysql_insert_dead_lock_retry_count) {
        continue;
      }

      std::ostringstream oss;
      get_mysql_exception_msg(oss, e, __FILE__, __FUNCTION__, __LINE__);
      oss << ", tried " << cnt << ", sql: " << sql_;
      LOG(ERROR) << oss.str();
      return 1;
    } catch (...) {
      ASSERT(false);
      return 1;
    }

    break; /* succeed */
  }
  return 0;
}

/******************************************************************************
 * MYSQLConn
 *
 ******************************************************************************/
int MYSQLConn::init(sql::Driver *mysql_driver) {
  ASSERT(mysql_driver);
  try {
    std::ostringstream conn_oss;
    conn_oss << "tcp://" << conf_.host << ":" << conf_.port;
    std::string conn_str = conn_oss.str();

    sql::Connection *ptr =
        mysql_driver->connect(conn_str, conf_.user, conf_.password);
    my_conn_.reset(ptr);
  } catch (...) {
    /* clang-format off */
    LOG(ERROR) << "Fail to connect to mysql"
               << ", host: " << conf_.host
               << ", socket: " << conf_.socket
               << ", port: " << conf_.port
               << ", user: " << conf_.user
               << ", password: " << conf_.password
               << ", default_schema: " << conf_.default_schema;
    /* clang-format on */
    return 1;
  }

  if (conf_.default_schema.size()) {
    my_conn_->setSchema(conf_.default_schema);
  }
  return 0;
}

std::unique_ptr<sql::Statement> MYSQLConn::create_mysql_stmt() const {
  if (!my_conn_) {
    return nullptr;
  }

  sql::Statement *stmt = nullptr;
  try {
    stmt = my_conn_->createStatement();
  } catch (sql::SQLException &e) {
    std::ostringstream oss;
    get_mysql_exception_msg(oss, e, __FILE__, __FUNCTION__, __LINE__);
    oss << ", sql: " << stmt;
    LOG(ERROR) << oss.str();
    return nullptr;
  } catch (...) {
    ASSERT(false);
    return nullptr;
  }

  std::unique_ptr<sql::Statement> res(stmt);
  return res;
}

int MYSQLConn::execute_stmt(const std::string &sql) const {
  auto stmt = create_mysql_stmt();
  size_t cnt = 1;
  for (; cnt < FLAGS_mysql_insert_dead_lock_retry_count; ++cnt) {
    try {
      stmt->execute(sql);
    } catch (sql::SQLException &e) {
      bool is_deadlock = (e.getErrorCode() == 1213);
      bool is_lock_wait_timeout = (e.getErrorCode() == 1205);
      bool is_mysql_gone_away = (e.getErrorCode() == 2006);
      bool is_recoverable =
          (is_deadlock || is_lock_wait_timeout || is_mysql_gone_away);
      if (is_recoverable && cnt < FLAGS_mysql_insert_dead_lock_retry_count) {
        continue;
      }

      std::ostringstream oss;
      get_mysql_exception_msg(oss, e, __FILE__, __FUNCTION__, __LINE__);
      oss << ", tried " << cnt << ", sql: " << sql;
      LOG(ERROR) << oss.str();
      return 1;
    } catch (...) {
      ASSERT(false);
      return 1;
    }

    break; /* succeed */
  }
  return 0;
}

int MYSQLConn::ddl(const std::string &sql) const { return execute_stmt(sql); }

int MYSQLConn::dml(const std::string &sql) const { return execute_stmt(sql); }

std::unique_ptr<sql::ResultSet> MYSQLConn::query(const std::string &sql) const {
  auto stmt = create_mysql_stmt();
  if (!stmt) {
    return nullptr;
  }

  auto *ptr = stmt->executeQuery(sql);
  std::unique_ptr<sql::ResultSet> res(ptr);
  return res;
}

shared_ptr<MYSQLPS> MYSQLConn::create_prepare_stmt(const std::string &sql) {
  auto ps = make_shared<MYSQLPS>(shared_from_this(), my_conn_.get(), sql);
  if (ps->init()) {
    return nullptr;
  }
  return ps;
}

/******************************************************************************
 * MYSQLConnMgr
 *
 ******************************************************************************/
int MYSQLConnMgr::init() {
  try {
    my_driver_ = get_driver_instance();
  } catch (...) {
    LOG(ERROR) << "Fail to get mysql driver instance";
    return 1;
  }
  ASSERT(my_driver_);
  return 0;
}

shared_ptr<MYSQLConn> MYSQLConnMgr::create_mysql_conn() const {
  auto res = std::shared_ptr<MYSQLConn>(new MYSQLConn(conf_));
  // auto res = make_shared<MYSQLConn>(conf_);
  if (res->init(my_driver_)) {
    return nullptr;
  }
  return res;
}

/******************************************************************************
 * MYSQLTableLock
 *
 ******************************************************************************/
int MYSQLTableLock::lock() {
  std::string lock_stmt;
  if (db_name_.size()) {
    lock_stmt = fmt::format("LOCK TABLES `{}`.`{}` WRITE", db_name_, tbl_name_);
  } else {
    lock_stmt = fmt::format("LOCK TABLES `{}` WRITE", tbl_name_);
  }
  if (conn_->dml(lock_stmt)) {
    LOG(WARNING) << "Fail to lock table"
                 << ", database=" << db_name_ << ", table=" << tbl_name_;
    return 1;
  }
  locked_ = true;
  return 0;
}

MYSQLTableLock::~MYSQLTableLock() {
  if (!locked_) return;
  if (conn_->dml("UNLOCK TABLES")) {
    LOG(WARNING) << "Fail to unlock table"
                 << ", database=" << db_name_ << ", table=" << tbl_name_;
  }
}
}  // namespace pview
