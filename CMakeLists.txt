cmake_minimum_required(VERSION 3.20)
project(grpc-libreoffice VERSION 0.2.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GRLIBRE_WERROR "Treat warnings in first-party code as errors" OFF)

add_library(grlibre_flags INTERFACE)
target_compile_options(grlibre_flags INTERFACE -Wall -Wextra -Wnon-virtual-dtor)
if(GRLIBRE_WERROR)
  target_compile_options(grlibre_flags INTERFACE -Werror -Wno-error=deprecated-declarations)
endif()

# LibreOfficeKit is header-only at build time; the worker dlopens the office
# core at runtime from GRLIBRE_LO_PATH.
set(GRLIBRE_LOK_INCLUDE_DIR "/usr/include" CACHE PATH
  "Directory containing the LibreOfficeKit/ header directory")
if(NOT EXISTS "${GRLIBRE_LOK_INCLUDE_DIR}/LibreOfficeKit/LibreOfficeKit.hxx")
  message(FATAL_ERROR "LibreOfficeKit headers not found under ${GRLIBRE_LOK_INCLUDE_DIR}; "
    "install libreofficekit-dev or set GRLIBRE_LOK_INCLUDE_DIR")
endif()

include(FetchContent)
set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(grpc
  GIT_REPOSITORY https://github.com/grpc/grpc.git
  GIT_TAG v1.82.1
  GIT_SHALLOW TRUE
  GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(grpc)

find_package(Threads REQUIRED)

# tcmalloc's per-thread caches serve the server's concurrent streams and the
# worker's paint/encode pipeline better than glibc malloc under contention.
option(GRLIBRE_TCMALLOC "Link tcmalloc_minimal into the binaries" ON)
if(GRLIBRE_TCMALLOC)
  find_library(GRLIBRE_TCMALLOC_LIB tcmalloc_minimal)
  if(NOT GRLIBRE_TCMALLOC_LIB)
    message(FATAL_ERROR "tcmalloc_minimal not found; install libgoogle-perftools-dev "
      "or configure with -DGRLIBRE_TCMALLOC=OFF")
  endif()
endif()

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(OFFICE_PROTO "${PROTO_DIR}/ai/pipestream/office/v1/office_service.proto")
set(DOCUMENT_PROTO "${PROTO_DIR}/ai/pipestream/document/v1/document.proto")
add_custom_command(
  OUTPUT "${GENERATED_DIR}/ai/pipestream/office/v1/office_service.pb.cc"
         "${GENERATED_DIR}/ai/pipestream/office/v1/office_service.pb.h"
         "${GENERATED_DIR}/ai/pipestream/office/v1/office_service.grpc.pb.cc"
         "${GENERATED_DIR}/ai/pipestream/office/v1/office_service.grpc.pb.h"
         "${GENERATED_DIR}/ai/pipestream/document/v1/document.pb.cc"
         "${GENERATED_DIR}/ai/pipestream/document/v1/document.pb.h"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_DIR}"
  COMMAND $<TARGET_FILE:protoc>
  ARGS --proto_path=${PROTO_DIR}
       --proto_path=${protobuf_SOURCE_DIR}/src
       --cpp_out=${GENERATED_DIR}
       --grpc_out=${GENERATED_DIR}
       --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
       ${OFFICE_PROTO} ${DOCUMENT_PROTO}
  DEPENDS ${OFFICE_PROTO} ${DOCUMENT_PROTO} protoc grpc_cpp_plugin
  VERBATIM)
add_library(office_proto
  "${GENERATED_DIR}/ai/pipestream/office/v1/office_service.pb.cc"
  "${GENERATED_DIR}/ai/pipestream/office/v1/office_service.grpc.pb.cc"
  "${GENERATED_DIR}/ai/pipestream/document/v1/document.pb.cc")
target_include_directories(office_proto PUBLIC "${GENERATED_DIR}")
target_link_libraries(office_proto PUBLIC grpc++)

add_library(grlibre_core
  src/event_frame.cpp
  src/worker_runner.cpp
  src/render_service.cpp)
target_include_directories(grlibre_core PUBLIC src)
target_link_libraries(grlibre_core
  PUBLIC office_proto Threads::Threads
  PRIVATE grlibre_flags)

add_executable(grlibre-server src/main.cpp)
target_link_libraries(grlibre-server PRIVATE grlibre_core grpc++_reflection grlibre_flags)
if(GRLIBRE_TCMALLOC)
  target_link_libraries(grlibre-server PRIVATE ${GRLIBRE_TCMALLOC_LIB})
endif()

add_executable(grlibre-worker
  src/worker_main.cpp
  src/lok_engine.cpp
  src/png_encode.cpp
  src/event_frame.cpp)
target_include_directories(grlibre-worker PRIVATE
  src
  "${GRLIBRE_LOK_INCLUDE_DIR}"
  "${GRLIBRE_LOK_INCLUDE_DIR}/LibreOfficeKit"
  third_party/stb)
target_link_libraries(grlibre-worker PRIVATE office_proto ${CMAKE_DL_LIBS} Threads::Threads grlibre_flags)
if(GRLIBRE_TCMALLOC)
  target_link_libraries(grlibre-worker PRIVATE ${GRLIBRE_TCMALLOC_LIB})
endif()

if(BUILD_TESTING)
  set(GRLIBRE_TESTS event-frame-test png-encode-test worker-render-test render-service-test)
  add_executable(event-frame-test tests/event_frame_test.cpp src/event_frame.cpp)
  target_include_directories(event-frame-test PRIVATE src)
  target_link_libraries(event-frame-test PRIVATE Threads::Threads grlibre_flags)

  add_executable(png-encode-test tests/png_encode_test.cpp src/png_encode.cpp)
  target_include_directories(png-encode-test PRIVATE src third_party/stb)
  target_link_libraries(png-encode-test PRIVATE grlibre_flags)

  add_executable(worker-render-test tests/worker_render_test.cpp)
  target_link_libraries(worker-render-test PRIVATE grlibre_core grlibre_flags)

  add_executable(render-service-test tests/render_service_test.cpp)
  target_link_libraries(render-service-test PRIVATE grlibre_core grlibre_flags)

  foreach(test_name ${GRLIBRE_TESTS})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
  set_tests_properties(worker-render-test render-service-test PROPERTIES
    ENVIRONMENT "GRLIBRE_WORKER=$<TARGET_FILE:grlibre-worker>"
    SKIP_RETURN_CODE 77)
endif()
