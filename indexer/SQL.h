//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

namespace pview {
/** If modified these names, please modify SQL.cc/SQL.h */
constexpr const char *kPViewIndexDB = "pview_index_database";

extern const char *SQL_create_pview_db;
/**
 * MYSQL table projects
 */
extern const char *SQL_projects_create;
extern const char *SQL_get_project_id;
extern const char *SQL_max_project_id;
extern const char *SQL_insert_new_project;

/**
 * MYSQL table for filename
 */
extern const char *SQL_filepaths_create;
extern const char *SQL_query_filepath_id;
extern const char *SQL_replace_filepath;
extern const char *SQL_is_file_exists;
extern const char *SQL_get_file_mtime;
extern const char *SQL_is_file_obselete;
extern const char *SQL_max_filepath_id;

/**
 * MYSQL table for FuncDef
 */
extern const char *SQL_func_def_create;
extern const char *SQL_func_def_insert_header;
extern const char *SQL_func_def_insert_param;
extern const char *SQL_query_func_def_id;
extern const char *SQL_query_func_def_usr;
extern const char *SQL_query_func_def_using_qualified;
extern const char *SQL_max_func_def_id;

/**
 * MYSQL table for FuncCall
 */
extern const char *SQL_func_call_create;
extern const char *SQL_func_call_insert_header;
extern const char *SQL_func_call_insert_param;
extern const char *SQL_query_func_call_info;
extern const char *SQL_query_func_call_info_with_throw;
extern const char *SQL_query_caller_usr_using_qualified;
extern const char *SQL_max_func_call_id;
}  // namespace pview
