cmake_minimum_required(VERSION 3.16)

project(aura_service VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

  # 指定按配置区分的库路径，避免误用 Debug 归档
  set(AURA_VCPKG_PREFIX "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
  set(JSONCPP_INCLUDE_DIRS "${AURA_VCPKG_PREFIX}/include")
  set(JSONCPP_LIBRARIES
      "$<$<CONFIG:Debug>:${AURA_VCPKG_PREFIX}/debug/lib/jsoncpp.lib>;$<$<NOT:$<CONFIG:Debug>>:${AURA_VCPKG_PREFIX}/lib/jsoncpp.lib>")
  set(BROTLI_INCLUDE_DIR "${AURA_VCPKG_PREFIX}/include")
  set(BROTLIDEC_LIBRARY
      "$<$<CONFIG:Debug>:${AURA_VCPKG_PREFIX}/debug/lib/brotlidec.lib>;$<$<NOT:$<CONFIG:Debug>>:${AURA_VCPKG_PREFIX}/lib/brotlidec.lib>")
  set(BROTLIENC_LIBRARY
      "$<$<CONFIG:Debug>:${AURA_VCPKG_PREFIX}/debug/lib/brotlienc.lib>;$<$<NOT:$<CONFIG:Debug>>:${AURA_VCPKG_PREFIX}/lib/brotlienc.lib>")
  set(BROTLICOMMON_LIBRARY
      "$<$<CONFIG:Debug>:${AURA_VCPKG_PREFIX}/debug/lib/brotlicommon.lib>;$<$<NOT:$<CONFIG:Debug>>:${AURA_VCPKG_PREFIX}/lib/brotlicommon.lib>")
endif()

find_package(Drogon CONFIG REQUIRED)
if(MSVC)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
else()
  find_package(OpenSSL REQUIRED)
endif()

set(AURA_LIBBCRYPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libbcrypt")

# 直接编译可移植 C 源，保证 MSVC 兼容
add_library(aura_bcrypt STATIC
  ${AURA_LIBBCRYPT_DIR}/src/bcrypt.c
  ${AURA_LIBBCRYPT_DIR}/src/crypt_blowfish.c
  ${AURA_LIBBCRYPT_DIR}/src/crypt_gensalt.c
  ${AURA_LIBBCRYPT_DIR}/src/wrapper.c
)
target_include_directories(aura_bcrypt PUBLIC
  ${AURA_LIBBCRYPT_DIR}/include
  ${AURA_LIBBCRYPT_DIR}/include/bcrypt
)
if(MSVC)
  target_compile_options(aura_bcrypt PRIVATE /FIio.h)
  target_compile_definitions(aura_bcrypt PRIVATE close=_close read=_read)
  target_link_libraries(aura_bcrypt PRIVATE Advapi32)
endif()

add_executable(AuraService
  src/main.cpp
  src/api.cpp
  src/auth.cpp
  src/database.cpp
)

if(NOT MSVC)
  set(AURA_LIVE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../algorithm" CACHE PATH
      "Directory containing libaura.a and csrc (portable C pipeline)")
  find_library(AURA_LIVE_CORE_LIB NAMES aura
               HINTS ${AURA_LIVE_CORE_DIR} NO_DEFAULT_PATH)
  if(AURA_LIVE_CORE_LIB AND EXISTS "${AURA_LIVE_CORE_DIR}/csrc/aura_pipeline.h")
    target_sources(AuraService PRIVATE src/live.cpp)
    target_include_directories(AuraService PRIVATE ${AURA_LIVE_CORE_DIR}/csrc)
    target_compile_definitions(AuraService PRIVATE AURA_HAS_LIVE_CORE)
    target_link_libraries(AuraService PRIVATE ${AURA_LIVE_CORE_LIB} m)
  endif()

  set(AURA_RADAR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../radar" CACHE PATH
      "Directory containing the RS6240 Linux transport sources")
  if(AURA_LIVE_CORE_LIB AND EXISTS "${AURA_RADAR_DIR}/include/rs6240_capture.h")
    target_sources(AuraService PRIVATE
      src/radar_live.cpp
      ${AURA_RADAR_DIR}/src/rs6240_spidev.c
      ${AURA_RADAR_DIR}/src/rs6240_hif.c
      ${AURA_RADAR_DIR}/src/rs6240_cube.c
      ${AURA_RADAR_DIR}/src/rs6240_roi.c
      ${AURA_RADAR_DIR}/src/rs6240_capture.c)
    target_include_directories(AuraService PRIVATE ${AURA_RADAR_DIR}/include)
    target_compile_definitions(AuraService PRIVATE AURA_HAS_RADAR
                               RS6240_ROI_READ_BLOCK_SIZE=576u)
  endif()
endif()

target_include_directories(AuraService PRIVATE include)
target_link_libraries(AuraService PRIVATE
  Drogon::Drogon
  aura_bcrypt
)
if(MSVC)
  target_link_libraries(AuraService PRIVATE unofficial::sqlite3::sqlite3 Bcrypt)
else()
  target_link_libraries(AuraService PRIVATE sqlite3 OpenSSL::Crypto)
endif()

if(MSVC)
  target_compile_options(AuraService PRIVATE /W4 /permissive- /utf-8)
  target_compile_definitions(AuraService PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(AuraService PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(aura_auth_tests
    tests/auth_tests.cpp
    src/auth.cpp
    src/database.cpp
  )
  target_include_directories(aura_auth_tests PRIVATE include)
  if(MSVC)
    target_link_libraries(aura_auth_tests PRIVATE
      Drogon::Drogon unofficial::sqlite3::sqlite3 aura_bcrypt Bcrypt)
  else()
    target_link_libraries(aura_auth_tests PRIVATE
      Drogon::Drogon sqlite3 aura_bcrypt OpenSSL::Crypto)
  endif()
  if(MSVC)
    target_compile_options(aura_auth_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(aura_auth_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  endif()
  add_test(NAME aura_auth_tests COMMAND aura_auth_tests)
endif()

install(TARGETS AuraService RUNTIME DESTINATION .)
