set(
  IRODS_API_PLUGIN_SOURCES_api_plugin_adapter_test_server
  ${CMAKE_SOURCE_DIR}/src/libirods_api_plugin_adapter_test.cpp
  )

set(
  IRODS_API_PLUGIN_SOURCES_api_plugin_adapter_test_client
  ${CMAKE_SOURCE_DIR}/src/libirods_api_plugin_adapter_test.cpp
  )

set(
  IRODS_API_PLUGIN_COMPILE_DEFINITIONS_api_plugin_adapter_test_server
  RODS_SERVER
  ENABLE_RE
  )

set(
  IRODS_API_PLUGIN_COMPILE_DEFINITIONS_api_plugin_adapter_test_client
  )

set(
  IRODS_API_PLUGIN_LINK_LIBRARIES_api_plugin_adapter_test_server
  irods_client
  irods_server
  irods_common
  irods_plugin_dependencies
  )

set(
  IRODS_API_PLUGIN_LINK_LIBRARIES_api_plugin_adapter_test_client
  irods_client
  irods_common
  irods_plugin_dependencies
  )

set(
  IRODS_API_PLUGINS
  api_plugin_adapter_test_server
  api_plugin_adapter_test_client
  )

foreach(PLUGIN ${IRODS_API_PLUGINS})
  add_library(
    ${PLUGIN}
    MODULE
    ${IRODS_API_PLUGIN_SOURCES_${PLUGIN}}
    ${CMAKE_BINARY_DIR}/include/irods_api_plugin_adapter_test_request.hpp
    ${CMAKE_BINARY_DIR}/include/irods_api_envelope.hpp
    )

  target_include_directories(
    ${PLUGIN}
    PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
    ${IRODS_INCLUDE_DIRS}
    ${IRODS_EXTERNALS_FULLPATH_BOOST}/include
    ${IRODS_EXTERNALS_FULLPATH_JANSSON}/include
    ${IRODS_EXTERNALS_FULLPATH_ARCHIVE}/include
    ${IRODS_EXTERNALS_FULLPATH_AVRO}/include
    ${IRODS_EXTERNALS_FULLPATH_ZMQ}/include
    ${IRODS_EXTERNALS_FULLPATH_CPPZMQ}/include
    )

  target_link_libraries(
    ${PLUGIN}
    PRIVATE
    ${CMAKE_BINARY_DIR}/libirods_api_endpoint.so
    ${IRODS_API_PLUGIN_LINK_LIBRARIES_${PLUGIN}}
    ${IRODS_EXTERNALS_FULLPATH_AVRO}/lib/libavrocpp.so
    ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_filesystem.so
    ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_system.so
    ${IRODS_EXTERNALS_FULLPATH_ARCHIVE}/lib/libarchive.so
    ${IRODS_EXTERNALS_FULLPATH_ZMQ}/lib/libzmq.so
    ${OPENSSL_CRYPTO_LIBRARY}
    ${CMAKE_DL_LIBS}
    )

  target_compile_definitions(${PLUGIN} PRIVATE ${IRODS_API_PLUGIN_COMPILE_DEFINITIONS_${PLUGIN}} ${IRODS_COMPILE_DEFINITIONS} BOOST_SYSTEM_NO_DEPRECATED)
  target_compile_options(${PLUGIN} PRIVATE -Wno-write-strings)
  set_property(TARGET ${PLUGIN} PROPERTY CXX_STANDARD ${IRODS_CXX_STANDARD})


  install(
    TARGETS
    ${PLUGIN}
    LIBRARY
    DESTINATION usr/lib/irods/plugins/api_v5
    )
endforeach()
