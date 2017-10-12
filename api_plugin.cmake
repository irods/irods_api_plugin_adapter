set(
  IRODS_API_PLUGIN_SOURCES_api_plugin_adapter_server
  ${CMAKE_SOURCE_DIR}/src/libapi_plugin_adapter.cpp
  )

set(
  IRODS_API_PLUGIN_SOURCES_api_plugin_adapter_client
  ${CMAKE_SOURCE_DIR}/src/libapi_plugin_adapter.cpp
  )

set(
  IRODS_API_PLUGIN_COMPILE_DEFINITIONS_api_plugin_adapter_server
  RODS_SERVER
  ENABLE_RE
  )

set(
  IRODS_API_PLUGIN_COMPILE_DEFINITIONS_api_plugin_adapter_client
  )

set(
  IRODS_API_PLUGIN_LINK_LIBRARIES_api_plugin_adapter_server
  irods_client
  irods_server
  irods_common
  irods_plugin_dependencies
  )

set(
  IRODS_API_PLUGIN_LINK_LIBRARIES_api_plugin_adapter_client
  irods_client
  irods_common
  irods_plugin_dependencies
  )

set(
  IRODS_API_PLUGINS
  api_plugin_adapter_server
  api_plugin_adapter_client
  )

add_library(
  irods_api_endpoint
  SHARED
  src/irods_api_endpoint.cpp
  ${CMAKE_BINARY_DIR}/include/irods_api_envelope.hpp
  )

set_property(TARGET irods_api_endpoint PROPERTY CXX_STANDARD ${IRODS_CXX_STANDARD})

target_include_directories(
  irods_api_endpoint
  PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/include
  ${IRODS_INCLUDE_DIRS}
  ${IRODS_EXTERNALS_FULLPATH_BOOST}/include
  ${IRODS_EXTERNALS_FULLPATH_JANSSON}/include
  ${IRODS_EXTERNALS_FULLPATH_ARCHIVE}/include
  ${IRODS_EXTERNALS_FULLPATH_AVRO}/include
  ${IRODS_EXTERNALS_FULLPATH_ZMQ}/include
  ${IRODS_EXTERNALS_FULLPATH_CPPZMQ}/include
  )

target_link_libraries(
  irods_api_endpoint
  PRIVATE
  ${IRODS_API_PLUGIN_LINK_LIBRARIES_${PLUGIN}}
  ${IRODS_EXTERNALS_FULLPATH_AVRO}/lib/libavrocpp.so
  ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_filesystem.so
  ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_system.so
  ${IRODS_EXTERNALS_FULLPATH_ARCHIVE}/lib/libarchive.so
  ${IRODS_EXTERNALS_FULLPATH_ZMQ}/lib/libzmq.so
  ${OPENSSL_CRYPTO_LIBRARY}
  ${CMAKE_DL_LIBS}
  )

install(
  TARGETS
  irods_api_endpoint
  LIBRARY
  DESTINATION usr/lib/
  )

install(
     FILES
     ${CMAKE_BINARY_DIR}/include/irods_api_envelope.hpp
     ${CMAKE_SOURCE_DIR}/include/irods_api_endpoint.hpp
     ${CMAKE_SOURCE_DIR}/include/irods_message_broker.hpp
     DESTINATION usr/include/irods
    )

foreach(PLUGIN ${IRODS_API_PLUGINS})
  add_library(
    ${PLUGIN}
    MODULE
    ${IRODS_API_PLUGIN_SOURCES_${PLUGIN}}
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
    irods_api_endpoint
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
    DESTINATION usr/lib/irods/plugins/api
    )
endforeach()
