//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#include "IndexCtx.h"
#include "SQL.h"
#include "StrUtils.h"

namespace pview {
IndexCtx *IndexCtx::get_instance() {
  static IndexCtx ctx_;
  return &ctx_;
}

void IndexCtx::set_project_table_names(const std::string &filepath_table,
                                       const std::string &func_defs_table,
                                       const std::string &func_calls_table) {
  filepath_table_ = filepath_table;
  func_defs_table_ = func_defs_table;
  func_calls_table_ = func_calls_table;
  ASSERT(filepath_table_.size() && func_defs_table_.size() &&
         func_calls_table_.size());

  /**
   * MYSQL table for filename
   */
  sql_filepaths_create_ =
      replace_string(SQL_filepaths_create, "{filepaths_tbl}", filepath_table_);
  sql_query_filepath_id_ =
      replace_string(SQL_query_filepath_id, "{filepaths_tbl}", filepath_table_);
  sql_replace_filepath_ =
      replace_string(SQL_replace_filepath, "{filepaths_tbl}", filepath_table_);
  sql_is_file_exists_ =
      replace_string(SQL_is_file_exists, "{filepaths_tbl}", filepath_table_);
  sql_get_file_mtime_ =
      replace_string(SQL_get_file_mtime, "{filepaths_tbl}", filepath_table_);
  sql_is_file_obselete_ =
      replace_string(SQL_is_file_obselete, "{filepaths_tbl}", filepath_table_);
  sql_max_filepath_id_ =
      replace_string(SQL_max_filepath_id, "{filepaths_tbl}", filepath_table_);

  /**
   * MYSQL table for FuncDef
   */
  sql_func_def_create_ = replace_string(
      SQL_func_def_create, "{func_definitions_tbl}", func_defs_table_);
  sql_func_def_insert_header_ = replace_string(
      SQL_func_def_insert_header, "{func_definitions_tbl}", func_defs_table_);
  sql_func_def_insert_param_ = replace_string(
      SQL_func_def_insert_param, "{func_definitions_tbl}", func_defs_table_);
  sql_query_func_def_id_ = replace_string(
      SQL_query_func_def_id, "{func_definitions_tbl}", func_defs_table_);
  sql_query_func_def_usr_ = replace_string(
      SQL_query_func_def_usr, "{func_definitions_tbl}", func_defs_table_);
  sql_query_func_def_using_qualified_ =
      replace_string(SQL_query_func_def_using_qualified,
                     "{func_definitions_tbl}", func_defs_table_);
  sql_max_func_def_id_ = replace_string(
      SQL_max_func_def_id, "{func_definitions_tbl}", func_defs_table_);

  /**
   * MYSQL table for FuncCall
   */
  sql_func_call_create_ = replace_string(SQL_func_call_create,
                                         "{func_calls_tbl}", func_calls_table_);
  sql_func_call_insert_header_ = replace_string(
      SQL_func_call_insert_header, "{func_calls_tbl}", func_calls_table_);
  sql_func_call_insert_param_ = replace_string(
      SQL_func_call_insert_param, "{func_calls_tbl}", func_calls_table_);
  sql_query_func_call_info_ = replace_string(
      SQL_query_func_call_info, "{func_calls_tbl}", func_calls_table_);
  sql_query_func_call_info_with_throw_ =
      replace_string(SQL_query_func_call_info_with_throw, "{func_calls_tbl}",
                     func_calls_table_);
  sql_query_caller_usr_using_qualified_ =
      replace_string(SQL_query_caller_usr_using_qualified, "{func_calls_tbl}",
                     func_calls_table_);
  sql_max_func_call_id_ = replace_string(SQL_max_func_call_id,
                                         "{func_calls_tbl}", func_calls_table_);
}
}  // namespace pview
