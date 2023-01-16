INCLUDE(ExternalProject)

MESSAGE(STATUS "============================================")
MESSAGE(STATUS "=           Building mysql connector       =")
MESSAGE(STATUS "============================================")

SET(PVIEW_MYSQL_CONN_DIR         "${CMAKE_SOURCE_DIR}/extra/mysql-connector/")
SET(PVIEW_MYSQL_CONN_TAR_BALL    "${CMAKE_SOURCE_DIR}/extra/mysql-connector/mysql-connector-cpp-8.0.13.tar.gz")
SET(PVIEW_MYSQL_CONN_SOURCE_DIR  "${CMAKE_SOURCE_DIR}/extra/mysql-connector/mysql-connector-cpp-8.0.13/")
SET(PVIEW_MYSQL_CONN_BUILD_DIR   "${CMAKE_SOURCE_DIR}/extra/mysql-connector/mysql-connector-cpp-8.0.13/build")
SET(PVIEW_MYSQL_CONN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/mysql-connector")

IF(CMAKE_GENERATOR MATCHES "Makefiles")
  SET(MAKE_COMMAND ${CMAKE_MAKE_PROGRAM} -j)
ELSE() # Xcode/Ninja generators
  SET(MAKE_COMMAND make)
ENDIF()

# Note: use mysql-server's boost
SET(PVIEW_MYSQL_CONN_CMAKE
  cmake ${PVIEW_MYSQL_CONN_SOURCE_DIR}
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1
    -DCMAKE_INSTALL_PREFIX=${PVIEW_MYSQL_CONN_INSTALL_DIR}
    -DWITH_JDBC=ON
    -DCMAKE_EXE_LINKER_FLAGS=" -static-libstdc++ -static-libgcc "
    -DWITH_BOOST=${CMAKE_SOURCE_DIR}/extra/boost/boost_1_77_0/
    -DBUNDLE_DEPENDENCIES=1
    -DBUILD_STATIC=OFF
    -DMYSQLCLIENT_STATIC_LINKING=0
)
#-DBUILD_STATIC=ON
#-DMYSQLCLIENT_STATIC_LINKING=1

# Decompress source files
EXECUTE_PROCESS(
  COMMAND tar xf ${PVIEW_MYSQL_CONN_TAR_BALL}
  WORKING_DIRECTORY ${PVIEW_MYSQL_CONN_DIR}
  RESULT_VARIABLE mysql_conn_tar_result
)
# create build dir
EXECUTE_PROCESS(
  COMMAND mkdir -p ${PVIEW_MYSQL_CONN_BUILD_DIR}
  WORKING_DIRECTORY ${PVIEW_MYSQL_CONN_DIR}
  RESULT_VARIABLE mysql_conn_mkdir_result
)

ExternalProject_Add(pview_mysql_conn
  PREFIX     ${PVIEW_MYSQL_CONN_INSTALL_DIR}
  SOURCE_DIR ${PVIEW_MYSQL_CONN_SOURCE_DIR}
  BINARY_DIR ${PVIEW_MYSQL_CONN_BUILD_DIR}
  STAMP_DIR  ${PVIEW_MYSQL_CONN_BUILD_DIR}
  CONFIGURE_COMMAND ${PVIEW_MYSQL_CONN_CMAKE}
  BUILD_COMMAND  ${MAKE_COMMAND}
  INSTALL_COMMAND make install -j
)
ADD_DEPENDENCIES(pview_mysql_conn mysqld_binary)

ADD_LIBRARY(mysql_conn_lib OBJECT IMPORTED)
SET_TARGET_PROPERTIES(
  mysql_conn_lib
  PROPERTIES IMPORTED_LOCATION "${PVIEW_MYSQL_CONN_INSTALL_DIR}/lib64/libmysqlcppconn8-static.a"
)
ADD_DEPENDENCIES(mysql_conn_lib pview_mysql_conn)
