file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/include/irods_api_plugin_adapter_test_request.hpp
    COMMAND ${IRODS_EXTERNALS_FULLPATH_AVRO}/bin/avrogencpp -n irods -o ${CMAKE_BINARY_DIR}/include/irods_api_plugin_adapter_test_request.hpp -i ${CMAKE_SOURCE_DIR}/avro_schemas/irods_api_plugin_adapter_test_request.json
    MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/avro_schemas/irods_api_plugin_adapter_test_request.json
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/include/irods_api_envelope.hpp
    COMMAND ${IRODS_EXTERNALS_FULLPATH_AVRO}/bin/avrogencpp -n irods -o ${CMAKE_BINARY_DIR}/include/irods_api_envelope.hpp -i ${CMAKE_SOURCE_DIR}/avro_schemas/irods_api_envelope.json
    MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/avro_schemas/irods_api_envelope.json
)

