############################################################################
# CMakeLists.txt
# Copyright (C) 2010-2026 Belledonne Communications, Grenoble France
#
############################################################################
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
############################################################################

cmake_minimum_required(VERSION 3.22)

# CMP0076 is required to use relative path in target_sources.
cmake_policy(SET CMP0076 NEW)
# CMP0077 is required to correctly force the value of subprojects' cache variables.
cmake_policy(SET CMP0077 NEW)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -O0 disables optimizations. Any level of optimization (higher than 0) will throw off debuggers while stepping through source code.
# With sanitizers enabled, fortifying source requires some optimizations. This is unwanted in Debug builds.
set(CMAKE_C_FLAGS_DEBUG_INIT "-g -O0 -U_FORTIFY_SOURCE -fdiagnostics-color=always")
set(CMAKE_CXX_FLAGS_DEBUG_INIT ${CMAKE_C_FLAGS_DEBUG_INIT})

include("./linphone-sdk/bctoolbox/cmake/BCToolboxCMakeUtils.cmake")
if(FLEXISIP_CONFERENCE_VERSION)
	message(WARNING "Ignoring git version, using provided \"${FLEXISIP_CONFERENCE_VERSION}\" instead")
	set(FLEXISIP_CONFERENCE_FULL_VERSION ${FLEXISIP_CONFERENCE_VERSION})
else()
	# Set project version by using the Git describe
	bc_compute_full_version(FLEXISIP_CONFERENCE_FULL_VERSION)
endif()
bc_parse_full_version("${FLEXISIP_CONFERENCE_FULL_VERSION}" major minor patch)
project(flexisip-conference VERSION "${major}.${minor}.${patch}" LANGUAGES C CXX)
unset(major)
unset(minor)
unset(patch)

include(CMakePushCheckState)
include(CMakeDependentOption)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(FeatureSummary)
include(CheckCXXSourceCompiles)
include(GNUInstallDirs)

# Force the compiler version to be at least what is used in our docker images with the lowest versions.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0)
	message(FATAL_ERROR "Flexisip requires at least gcc 13 (currently ${CMAKE_CXX_COMPILER_VERSION})")
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.0)
	message(FATAL_ERROR "Flexisip requires at least clang 19 (currently ${CMAKE_CXX_COMPILER_VERSION})")
endif()

include("flexisip/cmake/FlexisipUtils.cmake")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(IS_DEBUG TRUE)
else()
	set(IS_DEBUG FALSE)
endif()

option(ENABLE_SYSTEMD "Build with systemD support" ON)
option(ENABLE_SANITIZERS "Turn on sanitizers, like the LSAN memory leak detector" ${IS_DEBUG})
option(ENABLE_SUBMODULES_UBSAN "Turn on UBSAN on submodule code" OFF)
option(ENABLE_STRICT "Pass strict flags to the compiler" ON)
option(ENABLE_STRICT_LINPHONESDK "Pass strict flags to the compiler for all Linphone SDK submodules" OFF)
option(ENABLE_PDFDOC "Build PDF documentation" OFF)
option(ENABLE_REDIS_TLS "Enable TLS connections with Redis servers. Libhiredis may need to be compiled internally, depending on the version of the library available on your system" OFF)
option(INTERNAL_LIBHIREDIS "Build libhiredis source code present as Flexisip submodule instead of searching it in system libraries" OFF)
option(ENABLE_SNMP "Build with SNMP support" ON)
option(ENABLE_G729 "Build with G729 audio codec support" OFF) # Disabled by default for license conformity
option(ENABLE_UNIT_TESTS "Enable Flexisip unit tests (low level tests)" OFF)
cmake_dependent_option(ENABLE_UNIT_TESTS_NGHTTP2ASIO "Enable unit tests requiring libnghttp2_asio" ON "ENABLE_UNIT_TESTS" OFF)
add_ccache_option(ON)
option(ENABLE_COVERAGE "Enable flexisip clang test coverage reports (add instrumentation)" OFF)
option(INTERNAL_LIBSRTP2 "Build SRTP2 source code present as linphone-sdk submodule instead of searching it in system libraries" ON)
option(INTERNAL_JSONCPP "Build and use vendored Jsoncpp source code instead of searching for it in system libraries" OFF)
option(ENABLE_SOCI_POSTGRESQL_BACKEND "Build with SOCI Postgre sql backend support" ON)
option(ENABLE_EKT_SERVER "Enable Encrypted Key Transport server. For customers under a proprietary license, this functionality is under a specific license." OFF)


set(CPACK_GENERATOR "" CACHE STRING "Generator to use for making package. Supported values: 'RPM', 'DEB'")
set(SYSCONF_INSTALL_DIR "" CACHE STRING
	"Configuration directory, the place where Flexisip conference server expects its flexisip-conference.conf file to reside. Defaults to '${CMAKE_INSTALL_FULL_SYSCONFDIR}' if empty."
)
set(LOGROTATE_DIR "" CACHE STRING
	"The place where the logrotate file of the Flexisip conference server will be installed. Defaults to '${CMAKE_INSTALL_FULL_SYSCONFDIR}/logrotate' if empty."
)
set(FLEXISIP_SYSTEMD_INSTALL_DIR "" CACHE STRING
	"Where to install the SystemD units. Defaults to '${CMAKE_INSTALL_FULL_DATAROOTDIR}/systemd/system' if empty."
)

# It seems -fsanitize=address and -Wuninitialized don't play well together
if(CMAKE_BUILD_TYPE STREQUAL "Sanitizer" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105616
	if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS_EQUAL 13.2.1)
		set(HAS_GCC_BUG_105616 ON)
		# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105562
	elseif(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS_EQUAL 12.3.0)
		set(HAS_GCC_BUG_105562 ON)
	endif()
endif()

# Place the built libraries and executables in top level directories 'lib' and 'bin' in the build tree.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Archive output dir.")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Library output dir.")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "PDB (MSVC debug symbol)output dir.")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Executable/dll output dir.")

# Advanced options (i.e. hidden to the user by default)
option(ENABLE_LIBLINPHONE_TESTER "Build liblinphone_tester executable." OFF)
mark_as_advanced(ENABLE_LIBLINPHONE_TESTER)

# Handle the default value of installation paths. That ensures that they are
# always relative to the install prefix when the user hasn't set them explicitly.
if(SYSCONF_INSTALL_DIR STREQUAL "")
	set(SYSCONF_INSTALL_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}")
endif()
if(FLEXISIP_SYSTEMD_INSTALL_DIR STREQUAL "")
	set(FLEXISIP_SYSTEMD_INSTALL_DIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/systemd/system")
endif()
if(LOGROTATE_DIR STREQUAL "")
	set(LOGROTATE_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/logrotate")
endif()

# Build libflexisip-conference and all its dependencies as shared libraries
set(BUILD_SHARED_LIBS ON)

if(NOT CMAKE_INSTALL_RPATH AND CMAKE_INSTALL_PREFIX)
	set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR})
	if(APPLE)
		list(APPEND CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/Frameworks")
	endif()
	message(STATUS "Setting install rpath to ${CMAKE_INSTALL_RPATH}")
endif()

set(CONFIG_DIR "${SYSCONF_INSTALL_DIR}" CACHE STRING "Default flexisip-conference configuration directory")
message(STATUS "Config dir: ${CONFIG_DIR}")

set(INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(DEFAULT_LIB_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib")
set(DEFAULT_LOG_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/log")

function(FIND_PROGRAM_REQUIRED varname progname)
	find_program(${varname} NAMES "${progname}")
	if(NOT ${varname})
		message(FATAL_ERROR "Program '${progname}' is required but could not be found")
	endif()
endfunction()

list(APPEND CMAKE_MODULE_PATH
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake"
	"${CMAKE_CURRENT_SOURCE_DIR}/flexisip/cmake"
)

if(ENABLE_SANITIZERS)
	set(SANITIZERS_FLAG "-fsanitize=address")
	if (ENABLE_SUBMODULES_UBSAN)
		list(APPEND SANITIZERS_FLAG "-fsanitize=undefined")
	endif ()
	add_compile_options(
		${SANITIZERS_FLAG}
		"-fno-omit-frame-pointer"
		"-fno-optimize-sibling-calls"
	)
	add_link_options(${SANITIZERS_FLAG})
endif()

include("cmake/LinphoneSDK.cmake")
include("cmake/Flexisip.cmake")

if(ENABLE_SANITIZERS)
	set(MORE_SANITIZERS_FLAG "-fsanitize=undefined" "-fno-sanitize-recover=undefined")
	add_compile_options(${MORE_SANITIZERS_FLAG})
	add_link_options(${MORE_SANITIZERS_FLAG})
endif()

# Required packages
find_package(Threads)
find_package(XercesC)
if(APPLE)
	find_package(Iconv REQUIRED)
endif()

# Dummy executable used by the type-safe abstractions over POSIX processes
find_program(DUMMY_EXEC NAMES true REQUIRED)

find_file(HAVE_SYS_PRCTL_H NAMES sys/prctl.h)

set(CMAKE_REQUIRED_LIBRARIES)

# Options
if (ENABLE_SYSTEMD)
	find_package(LibSystemD REQUIRED)
endif()

if(ENABLE_SNMP)
	# todo: Not quite ready
	FIND_PROGRAM_REQUIRED(NET_SNMP_PROG net-snmp-config)
	find_path(NET_SNMP_INCLUDE_DIRS NAMES net-snmp/net-snmp-config.h)
	if(NOT NET_SNMP_INCLUDE_DIRS)
		message(FATAL_ERROR "SNMP header files not found")
	endif()

	execute_process(COMMAND "${NET_SNMP_PROG}" "--agent-libs" OUTPUT_VARIABLE NET_SNMP_LIBRARIES OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(NOT INTERNAL_LIBHIREDIS)
	find_package(Hiredis 1.1 REQUIRED)
endif()

if(ENABLE_PDFDOC)
	FIND_PROGRAM_REQUIRED(PDFLATEX_PROG pdflatex)
endif()

# Allow to use SLOGD and LOGD macros.
add_definitions("-DBCTBX_DEBUG_MODE")

find_package(OpenSSL 0.9.8 REQUIRED)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

include_directories(
	"include"
	"src"
	"${CMAKE_CURRENT_BINARY_DIR}"
	"${CMAKE_CURRENT_BINARY_DIR}/include"
	"${CMAKE_CURRENT_BINARY_DIR}/src"
)

set(BELR_GRAMMARS_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/belr/grammars")

configure_file(cmake/flexisip-conference-config.h.in flexisip-conference-config.h)
set_source_files_properties(${PROJECT_BINARY_DIR}/flexisip-conference-config.h PROPERTIES GENERATED ON)
add_compile_definitions("HAVE_CONFIG_H")

# Compute and set compilation options
bc_init_compilation_flags(CPP_BUILD_FLAGS C_BUILD_FLAGS CXX_BUILD_FLAGS ENABLE_STRICT)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	if(ENABLE_STRICT)
		list(APPEND CXX_BUILD_FLAGS
			"-Werror=unused-result" # Packaging (on Ubuntu) has this on, so better catch it sooner
			"-Werror=maybe-uninitialized" # GCC on CentOS 7 treats this as an error
		)
	endif()
	# -Werror=varargs seems to do false positives with GCC 4.9.x
	if(CMAKE_CXX_COMPILER_VERSION MATCHES "^4\\.9\\.[0-9]+$")
		list(APPEND CXX_BUILD_FLAGS "-Wno-error=varargs")
	endif()
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7)
		# Old compiler (gcc6 on Debian 9 Stretch) are giving us some toubles...
		list(APPEND CXX_BUILD_FLAGS "-Wno-error=unused-variable" "-Wno-error=attributes")
	else()
		# GCC on CentOS 7 treats this as an error
		list(APPEND CXX_BUILD_FLAGS "-Werror=format-truncation=1")
	endif()
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.1")
		# Enable std::filesystem on old implementations (GNU <9.1)
		link_libraries("stdc++fs")
	endif()
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
	# Old compiler (clang3 on Debian 9 Stretch) are giving us some toubles...
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4)
		list(APPEND CXX_BUILD_FLAGS "-Wno-error=unused-variable" "-Wno-error=unknown-attributes")
	endif()
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
		# Enable std::filesystem on old implementations (LLVM <9.0)
		link_libraries("stdc++fs")
	endif()
	if(APPLE AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "10.0")
		# Explicitly set GNUC version to 8.0.0 (full support for c++17).
		list(APPEND CXX_BUILD_FLAGS "-fgnuc-version=8.0.0")
	endif()
endif()

add_compile_options(${CPP_BUILD_FLAGS} ${CXX_BUILD_FLAGS})

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(scripts)

if(ENABLE_UNIT_TESTS)
	add_subdirectory(tester)
endif()

# Packaging
add_subdirectory(packaging)