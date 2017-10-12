add_executable(
  irods
  ${CMAKE_SOURCE_DIR}/src/irods.cpp
  ${CMAKE_BINARY_DIR}/include/irods_api_envelope.hpp
  )
target_link_libraries(
  irods
  PRIVATE
  irods_client
  irods_common
  irods_plugin_dependencies
  irods_api_endpoint
  ${IRODS_EXTERNALS_FULLPATH_AVRO}/lib/libavrocpp.so
  ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_filesystem.so
  ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_program_options.so
  ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_system.so
  ${IRODS_EXTERNALS_FULLPATH_JANSSON}/lib/libjansson.so
  ${IRODS_EXTERNALS_FULLPATH_ZMQ}/lib/libzmq.so
  )
target_include_directories(
  irods
  PRIVATE
  ${CMAKE_BINARY_DIR}/include
  ${CMAKE_SOURCE_DIR}/include
  ${IRODS_INCLUDE_DIRS}
  ${IRODS_EXTERNALS_FULLPATH_AVRO}/include
  ${IRODS_EXTERNALS_FULLPATH_BOOST}/include
  ${IRODS_EXTERNALS_FULLPATH_JANSSON}/include
  ${IRODS_EXTERNALS_FULLPATH_ARCHIVE}/include
  ${IRODS_EXTERNALS_FULLPATH_CPPZMQ}/include
  ${IRODS_EXTERNALS_FULLPATH_ZMQ}/include
  ${IRODS_EXTERNALS_FULLPATH_AVRO}/include
  )
target_compile_definitions(irods PRIVATE RODS_SERVER ${IRODS_COMPILE_DEFINITIONS} BOOST_SYSTEM_NO_DEPRECATED)
target_compile_options(irods PRIVATE -Wno-write-strings)
set_property(TARGET irods PROPERTY CXX_STANDARD ${IRODS_CXX_STANDARD})

install(
  TARGETS
  irods
  RUNTIME
  DESTINATION usr/bin
  )
