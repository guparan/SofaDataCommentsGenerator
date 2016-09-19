cmake_minimum_required(VERSION 3.2)
project(SofaDataDescriptionExtractor)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fno-rtti")


FIND_PACKAGE(Clang REQUIRED)

MESSAGE(STATUS ${CLANG_INSTALL_PREFIX})
include_directories(${CLANG_INSTALL_PREFIX}/include)

set(SOURCE_FILES
        src/main.cc)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} clangAST clangRewrite clangTooling clangASTMatchers clangBasic clangFrontend clangParse clangLex LLVMSupport)
