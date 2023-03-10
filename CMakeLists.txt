cmake_minimum_required(VERSION 3.15)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there")
endif()

project (doot_test)

set(CMAKE_CXX_STANDARD 17)

#instead of set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
#you can make an install command by install(TARGETS "project" RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
include(GNUInstallDirs)

#so you have 2 choices, override CMAKE_CXX_FLAGS_RELWITHDEBINFO so it changes the runtime, or set these flag.
option(FORCE_STATIC_VCRT "for msvc" OFF)
option(FORCE_NON_DEBUG_VCRT "for msvc" OFF)

#note that you cannot mix sanitizers with eachother.
#asan + ubsan + lsan should be a supported combination
option(USE_UBSAN "Use undefined behavior sanitizer" OFF)
option(USE_ASAN "Use address sanitizer" OFF)
option(USE_LSAN "Use leak sanitizer" OFF)
#I am unsure if TSAN would work, the FAQ say that C++11 threads are buggy 
#or require the libc/libstdc++ standard library to be built with LSAN
option(USE_TSAN "Use thread sanitizer" OFF)

#Control Flow Integrity is interesting, I wonder if cfi is closer to UBSAN or ASAN?
#-fsanitize=cfi

option(USE_LTO "Use link time optimizations" OFF)

#add_compile_options(-fprofile-instr-generate -fcoverage-mapping)

if(USE_UBSAN)
	add_compile_options(-fsanitize=undefined)
	add_compile_options(-fno-sanitize-recover=all)
	#this wont work on gcc for some reason.
	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fsanitize=unsigned-integer-overflow -fsanitize=nullability -fsanitize=implicit-conversion -fsanitize=local-bounds)
	endif()
	#add_compile_options(-fsanitize-trap=all)
endif ()

if(USE_ASAN)
#also use ASAN_OPTIONS=strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1
#but I don't really know of those settings make a difference.
    add_compile_options(-fsanitize=address)
    
    #whats the difference between this and -fcoverage-mapping?
	#add_compile_options(-fsanitize-coverage=trace-pc-guard)
	
	
endif()

if(USE_TSAN)
    add_compile_options(-fsanitize=thread)
endif()

if(USE_LSAN)
    add_compile_options(-fsanitize=leak)
endif()

if(USE_LTO)
	#for -flto, and also something in msvc.
	#the only problem is that you might want to customize the lto target, like -flto=thin on clang.
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WIN32))
	#CRASHRTP_HANDLER doesn't work on MINGW because it doesn't support codeview.
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DUSE_WIN32_CRASHRTP_HANDLER")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DUSE_WIN32_CRASHRTP_HANDLER")
endif()



if (MSVC)
	#keep asserts for reldeb
	string( REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244") # C4244: 'conversion_type': conversion from 'type1' to 'type2', possible loss of data
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4068") # C4068: unknown pragma
	#there are probably reasons for not disabling this warning, so consider lifting it once in a while.
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4267") # C4267: 'return': conversion from 'size_t' to 'int', possible loss of data
else()

	#keep asserts for reldeb
	string( REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
	
	#this is a big chunk taken from openrtc2, I hope they don't mind, but I like warnings.
    include(CheckCXXCompilerFlag)

    # Check if a flag exists and add it to the list of compiler options
    function (ADD_CHECK_CXX_COMPILER_FLAG _CXXFLAGS _CACHE_VAR _FLAG)
        CHECK_CXX_COMPILER_FLAG("${_FLAG}" "${_CACHE_VAR}")
        if (${_CACHE_VAR})
            set(${_CXXFLAGS} "${${_CXXFLAGS}} ${_FLAG}" PARENT_SCOPE)
        else ()
            message(STATUS "Unsupported CXXFLAG: ${_FLAG}")
        endif ()
    endfunction ()

	
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_SUGGEST_OVERRIDE -Wsuggest-override)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_DUPLICATED_COND -Wduplicated-cond)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_NON_VIRTUAL_DTOR -Wnon-virtual-dtor)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_MISSING_VARIABLE_DECLARATIONS -Wmissing-variable-declarations)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_DUPLICATED_BRANCHES -Wduplicated-branches)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_RESTRICT -Wrestrict)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_MISSING_FIELD_INITIALIZERS -Wmissing-field-initializers)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_UNREACHABLE_CODE_BREAK -Wunreachable-code-break)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_LOGICAL_OP -Wlogical-op)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_RANGE_LOOP_ANALYSIS -Wrange-loop-analysis)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_TAUTOLOGICAL_ZERO_COMPARE -Wtautological-unsigned-zero-compare)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WNO_CLOBBERED -Wno-clobbered)
	

    # Do not enable for MinGW, as its headers contain redundant declarations of builtin functions (from orct2)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_REDUNDANT_DECLS -Wredundant-decls)

    # Currently used MinGW w/GCC 7.2 doesn't provide sane error messages for this (from orct2)
    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_NULL_DEREFERENCE -Wnull-dereference)

	#these 2 warnings can be tested once in a while, but they can be annoying since final is not neccessary everywhere.
    #ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_SUGGEST_FINAL_TYPES -Wsuggest-final-types)
    #ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_SUGGEST_FINAL_METHODS -Wsuggest-final-methods)

    ADD_CHECK_CXX_COMPILER_FLAG(CMAKE_CXX_FLAGS CXX_WARN_IGNORED_QUALIFIERS -Wignored-qualifiers)
	
	
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstrict-aliasing -Wundef -Wmissing-declarations -Winit-self -Wall -Wextra -Wshadow -Werror=vla")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas -Wno-missing-braces")
	
endif()

#for clang on clang-cl
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Xlinker /STACK:10000000")

add_executable(${PROJECT_NAME} 
	code/main.cpp
	code/openal_wrapper.cpp
	code/openal_wrapper.h
	code/global.cpp
	code/global.h
	code/debug_tools.cpp
	code/debug_tools.h
	code/win32_crashrpt.cpp
	code/win32_crashrpt.h
	code/cvar.cpp
	code/cvar.h
	code/mini_tools.h
	code/SDL_wrapper.cpp
	code/SDL_wrapper.h
	code/json_wrapper.cpp
	code/json_wrapper.h
	code/gl_wrapper.cpp
	code/gl_wrapper.h

	code/opengles2/SDL_gles2funcs.h
	code/stb/stb_image.h
	code/wai/whereami.c
    code/wai/whereami.h
	
    code/tests/test_json.cpp
    
)

#I'm not using this to speed up compilation, 
#this just helps clangd to deal with non self-contained headers (but it's bugged in clangd 10.0).
#target_precompile_headers(${PROJECT_NAME} PRIVATE "code/global.h")

if(WIN32)
	set(WIN32_SILENCE_FLAGS NOMINMAX _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
	target_compile_definitions(${PROJECT_NAME} PUBLIC ${WIN32_SILENCE_FLAGS})
endif()

#
# VORBIS
#

find_package(Vorbis CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} Vorbis::vorbis Vorbis::vorbisenc Vorbis::vorbisfile)

#
# SDL2
#

#find_package(SDL2 CONFIG REQUIRED)
#if (WIN32)
#	target_link_libraries(${PROJECT_NAME} SDL2::SDL2main)
#endif()
#target_link_libraries(${PROJECT_NAME} SDL2::SDL2)
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES})

#
# OPENAL
#

find_package(OpenAL CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} OpenAL::OpenAL)
#find_package(OpenAL REQUIRED)
#include_directories(${OPENAL_INCLUDE_DIR})
#target_link_libraries(${PROJECT_NAME} ${OPENAL_LIBRARY})

#
# RAPIDJSON
#
find_package(RapidJSON CONFIG REQUIRED)

if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    
    #for XInitThreads, since I use a thread for the IO and a thread for the logic (can be disabled)
    target_link_libraries(${PROJECT_NAME} -lX11)
    
    #for addr2line, pie does help with security but it doesn't help with debugging. 
    #I bet there is a way to get a addr2line backtrace with pie since all that you need is the correct offset for the address, 
    #maybe libbacktrace or libunwind can magically handle it??? since backtrace_symbols works for some reason...
    target_link_libraries(${PROJECT_NAME} -no-pie)
    
    #TODO: is this not required by clang only?
    if(USE_ASAN)
        target_link_libraries(${PROJECT_NAME} -fsanitize=address)
    endif()
    if(USE_UBSAN)
        target_link_libraries(${PROJECT_NAME} -fsanitize=undefined)
    endif()
    if(USE_TSAN)
        target_link_libraries(${PROJECT_NAME} -fsanitize=thread)
    endif()
endif()




#note that just because a library is static does not mean it is using the static C runtime /MT
#if I wanted to I could link SDL2 statically while linking to other libraries dynamically.
if(FORCE_STATIC_VCRT)
	set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
elseif(FORCE_NON_DEBUG_VCRT)
	set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

install(TARGETS ${PROJECT_NAME})
if(MSVC)
	#copy the pdb files so the debugger can see them.
	#note that for some reason clang++ can't use this even though it makes PDB files.
	install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION bin OPTIONAL)
endif()

