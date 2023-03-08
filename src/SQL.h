#pragma once

namespace pview {
extern const char *SQL_create_pview_db;
/**
 * MYSQL table for filename
 */
extern const char *SQL_filepaths_create;
extern const char *SQL_query_filepath_id;
extern const char *SQL_replace_filepath;
extern const char *SQL_is_file_exists;
extern const char *SQL_is_file_obselete;
extern const char *SQL_max_filepath_id;

/**
 * MYSQL table for ClassDef
 */
extern const char *SQL_class_def_create;

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
