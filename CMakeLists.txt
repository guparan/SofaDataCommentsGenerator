cmake_minimum_required(VERSION 3.2)
project(SofaDataCommentsGenerator)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fno-rtti")

set(LLVM_LIBDIR ${LLVM_LIBDIR} CACHE PATH "LLVM lib directory" FORCE)
if(LLVM_LIBDIR STREQUAL "")
    message( FATAL_ERROR "LLVM_LIBDIR must be defined." )
endif()

set(Clang_DIR "${LLVM_LIBDIR}/cmake/clang" CACHE INTERNAL "Clang directory" FORCE)
set(LLVM_DIR "${LLVM_LIBDIR}/cmake/llvm" CACHE INTERNAL "LLVM directory" FORCE)

FIND_PACKAGE(Clang REQUIRED)

MESSAGE(STATUS ${CLANG_INSTALL_PREFIX})
include_directories(${CLANG_INSTALL_PREFIX}/include)

set(SOURCE_FILES
        main.cpp)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME}
    clangAST
    clangRewrite
    clangTooling
    clangASTMatchers
    clangBasic
    clangFrontend
    clangParse
    clangLex
    LLVMSupport
)
