cmake_minimum_required (VERSION 2.8.8)

project (screepsRoom)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -ggdb")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# use, i.e. don't skip the full RPATH for the build tree
SET(CMAKE_SKIP_BUILD_RPATH  FALSE)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE) 
SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
LIST(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
IF("${isSystemDir}" STREQUAL "-1")
   SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
ENDIF("${isSystemDir}" STREQUAL "-1")

find_package ( nlohmann_json REQUIRED
        NO_CMAKE_PACKAGE_REGISTRY
        NO_CMAKE_SYSTEM_PACKAGE_REGISTRY
)
include_directories(${JSON_INCLUDE_DIR})

find_package ( Simple-Web-Server REQUIRED
        NO_CMAKE_PACKAGE_REGISTRY
        NO_CMAKE_SYSTEM_PACKAGE_REGISTRY
)
include_directories(${SimpleWebServer_INCLUDE_DIRS})

find_package ( Simple-WebSocket-Server REQUIRED
        NO_CMAKE_PACKAGE_REGISTRY
        NO_CMAKE_SYSTEM_PACKAGE_REGISTRY
)
include_directories(${SimpleWebsocketServer_INCLUDE_DIRS})

find_package ( ScreepsApi REQUIRED
        NO_CMAKE_PACKAGE_REGISTRY
        NO_CMAKE_SYSTEM_PACKAGE_REGISTRY
)
include_directories(${ScreepsApi_INCLUDE_DIRS})

find_package ( ProgramApi REQUIRED
        NO_CMAKE_PACKAGE_REGISTRY
        NO_CMAKE_SYSTEM_PACKAGE_REGISTRY
)
include_directories(${ProgramApi_INCLUDE_DIRS})

find_package ( ScreepsApiV12 REQUIRED
        NO_CMAKE_PACKAGE_REGISTRY
        NO_CMAKE_SYSTEM_PACKAGE_REGISTRY
)
include_directories(${ScreepsApiV12_INCLUDE_DIRS})

link_directories ( ${ScreepsApi_LIBRARY_DIRS} )
link_directories ( ${ScreepsApiV12_LIBRARY_DIRS} )

find_package(Threads REQUIRED)

set(BOOST_COMPONENTS system thread filesystem date_time coroutine context)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.9)
        set(BOOST_COMPONENTS ${BOOST_COMPONENTS} regex)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_BOOST_REGEX")
    endif()
endif()
find_package(Boost 1.53.0 COMPONENTS ${BOOST_COMPONENTS} REQUIRED)
include_directories(SYSTEM ${Boost_INCLUDE_DIR})
if(APPLE)
  set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
endif()
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
message ( status "boost libs : ${Boost_LIBRARIES}" )

set (PROJECT_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set (PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
 
file(
        GLOB_RECURSE
        PROJECT_SRCS
        ${PROJECT_SOURCE_DIR}/*
)

include_directories("${PROJECT_INCLUDE_DIR}")

add_executable ( ${PROJECT_NAME} ${PROJECT_SRCS} )

target_link_libraries ( ${PROJECT_NAME} ProgramApi )
target_link_libraries ( ${PROJECT_NAME} ScreepsApi )
target_link_libraries ( ${PROJECT_NAME} ScreepsApiV12 )

target_link_libraries ( ${PROJECT_NAME} ${Boost_LIBRARIES} )
target_link_libraries ( ${PROJECT_NAME} ${OPENSSL_CRYPTO_LIBRARY} )
target_link_libraries ( ${PROJECT_NAME} ${OPENSSL_LIBRARIES})
target_link_libraries ( ${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
