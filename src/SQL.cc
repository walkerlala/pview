//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#include "SQL.h"

namespace pview {
const char *SQL_create_pview_db = R""""(
CREATE DATABASE IF NOT EXISTS `pview_index_database`
)"""";

/**
 * MYSQL table projects
 */
const char *SQL_projects_create = R""""(
CREATE TABLE IF NOT EXISTS
`pview_index_database`.`projects` (
  project_id BIGINT,
  project_root VARCHAR(2048),
  PRIMARY KEY (project_id),
  KEY k_project_root (project_root)
) CHARSET latin1 COLLATE latin1_bin ENGINE=InnoDB;
)"""";

const char *SQL_get_project_id = R""""(
SELECT project_id FROM `pview_index_database`.`projects`
WHERE project_root = "{arg_project_root}"
)"""";

const char *SQL_max_project_id = R""""(
SELECT MAX(project_id) FROM `pview_index_database`.`projects`
)"""";

const char *SQL_insert_new_project = R""""(
INSERT INTO `pview_index_database`.`projects` (
  project_id,
  project_root
) VALUES (
   {arg_project_id},
  "{arg_project_root}"
)
)"""";

/**
 * MYSQL table for filepath
 */
const char *SQL_filepaths_create = R""""(
CREATE TABLE IF NOT EXISTS
`pview_index_database`.`{filepaths_tbl}` (
  filepath_id BIGINT,
  filepath VARCHAR(2048),
  create_time DATETIME,
  PRIMARY KEY (filepath_id),
  KEY k_fp (filepath)
) CHARSET latin1 COLLATE latin1_bin ENGINE=InnoDB;
)"""";

const char *SQL_query_filepath_id = R""""(
SELECT filepath_id FROM `pview_index_database`.`{filepaths_tbl}`
WHERE filepath = "{arg_filepath}"
)"""";

const char *SQL_replace_filepath = R""""(
REPLACE INTO `pview_index_database`.`{filepaths_tbl}`
(
  filepath_id,
  filepath,
  create_time
)
VALUES
(
  {arg_filepath_id},
  "{arg_filepath}",
  FROM_UNIXTIME({arg_create_time})
)
)"""";

const char *SQL_is_file_exists = R""""(
SELECT COUNT(1) FROM `pview_index_database`.`{filepaths_tbl}`
WHERE filepath = "{arg_filepath}"
)"""";

const char *SQL_get_file_mtime = R""""(
SELECT UNIX_TIMESTAMP(MAX(create_time))
FROM `pview_index_database`.`{filepaths_tbl}`
WHERE filepath = "{arg_filepath}"
)"""";

const char *SQL_is_file_obselete = R""""(
SELECT COUNT(1) FROM `pview_index_database`.`{filepaths_tbl}`
WHERE FROM_UNIXTIME({arg_create_time}) > (
  SELECT MAX(create_time)
  FROM `pview_index_database`.`{filepaths_tbl}`
  WHERE filepath = "{arg_filepath}"
);
)"""";

const char *SQL_max_filepath_id = R""""(
SELECT MAX(filepath_id) FROM `pview_index_database`.`{filepaths_tbl}`
)"""";

/**
 * MYSQL table for FuncDef
 */
const char *SQL_func_def_create = R""""(
CREATE TABLE IF NOT EXISTS
`pview_index_database`.`{func_definitions_tbl}` (
  func_def_id bigint,
  create_time datetime,
  usr VARCHAR(512),
  qualified VARCHAR(512),
  might_throw tinyint,
  location_file_id bigint,
  location_line bigint,
  PRIMARY KEY(usr),
  KEY k_qualified (qualified, usr),
  KEY k_usr (usr, qualified, create_time)
) CHARSET latin1 COLLATE latin1_bin ENGINE=InnoDB;
)"""";

const char *SQL_func_def_insert_header = R""""(
INSERT IGNORE INTO `pview_index_database`.`{func_definitions_tbl}` (
  func_def_id,
  create_time,
  usr,
  qualified,
  might_throw,
  location_file_id,
  location_line
) VALUES
)"""";

const char *SQL_func_def_insert_param = R""""(
(
   {arg_func_def_id},
  FROM_UNIXTIME({arg_create_time}),
  "{arg_usr}",
  "{arg_qualified}",
   {arg_might_throw},
   {arg_location_file_id},
   {arg_location_line}
)
)"""";

const char *SQL_query_func_def_id = R""""(
SELECT func_def_id FROM `pview_index_database`.`{func_definitions_tbl}`
WHERE usr = "{arg_usr}"
)"""";

const char *SQL_query_func_def_usr = R""""(
SELECT usr FROM `pview_index_database`.`{func_definitions_tbl}`
WHERE qualified = "{arg_qualified}"
)"""";

const char *SQL_query_func_def_using_qualified = R""""(
SELECT qualified FROM `pview_index_database`.`{func_definitions_tbl}`
WHERE usr = "{arg_usr}"
ORDER BY create_time DESC
LIMIT 1;
)"""";

const char *SQL_max_func_def_id = R""""(
SELECT MAX(func_def_id) FROM `pview_index_database`.`{func_definitions_tbl}`
)"""";

/**
 * MYSQL table for FuncCall
 */
const char *SQL_func_call_create = R""""(
CREATE TABLE IF NOT EXISTS
`pview_index_database`.`{func_calls_tbl}` (
  func_call_id bigint,
  create_time datetime,
  caller_usr VARCHAR(512),
  caller_might_throw tinyint,
  usr VARCHAR(512),
  qualified VARCHAR(512),
  location_file_id bigint,
  location_line bigint,
  PRIMARY KEY (func_call_id),
  KEY k_caller_usr (caller_usr, usr, qualified),
  KEY k_qualified (qualified, caller_usr, usr)
) CHARSET latin1 COLLATE latin1_bin ENGINE=InnoDB;
)"""";

const char *SQL_func_call_insert_header = R""""(
INSERT IGNORE INTO `pview_index_database`.`{func_calls_tbl}` (
  func_call_id,
  create_time,
  caller_usr,
  caller_might_throw,
  usr,
  qualified,
  location_file_id,
  location_line
) VALUES
)"""";

const char *SQL_func_call_insert_param = R""""(
(
   {arg_func_call_id},
  FROM_UNIXTIME({arg_create_time}),
  "{arg_caller_usr}",
   {arg_caller_might_throw},
  "{arg_usr}",
  "{arg_qualified}",
   {arg_location_file_id},
   {arg_location_line}
)
)"""";

const char *SQL_query_func_call_info = R""""(
SELECT usr, qualified FROM `pview_index_database`.`{func_calls_tbl}`
WHERE caller_usr = "{arg_caller_usr}"
)"""";
const char *SQL_query_func_call_info_with_throw = R""""(
SELECT usr, qualified, caller_might_throw
FROM `pview_index_database`.`{func_calls_tbl}`
WHERE caller_usr = "{arg_caller_usr}"
)"""";

const char *SQL_query_caller_usr_using_qualified = R""""(
SELECT caller_usr FROM `pview_index_database`.`{func_calls_tbl}`
WHERE qualified = "{arg_qualified}"
)"""";

const char *SQL_max_func_call_id = R""""(
SELECT MAX(func_call_id) FROM `pview_index_database`.`{func_calls_tbl}`
)"""";
}  // namespace pview
