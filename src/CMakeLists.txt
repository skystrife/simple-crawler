add_executable(simple-crawler simple_crawler.cpp)
target_link_libraries(simple-crawler sc-definitions cpptoml
                      ${CPR_LIBRARIES}
                      ${LIBXML2_LIBRARIES}
                      gumbo-libxml
                      gumbo-parser)
target_include_directories(simple-crawler PUBLIC ${CPR_INCLUDE_DIRS}
                           ${GUMBO_INCLUDE_DIR}
                           ${GUMBO_LIBXML_INCLUDE_DIR}
                           ${LIBXML2_INCLUDE_DIR})
target_compile_definitions(simple-crawler PUBLIC ${LIBXML2_DEFINITIONS})
