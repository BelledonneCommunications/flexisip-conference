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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
############################################################################

#######################################################################################################################
# Build the Flexisip library
#######################################################################################################################

add_library(flexisip-conference
	conference/conference-server.cc
	conference/conference-server.hh
	conference/registration-subscription.cc
	conference/registration-subscription.hh
	conference/participant-registration-subscription-handler.cc
	conference/participant-registration-subscription-handler.hh
	main/flexisip-conference.hh
	main/flexisip-conference.cc
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(stun_c_compile_options "-Wno-error=sign-compare")
	if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8.0.0)
		list(APPEND stun_c_compile_options "-Wno-stringop-truncation")
	endif()
	set_source_files_properties(stun/stun.c PROPERTIES COMPILE_OPTIONS "${stun_c_compile_options}")
	unset(stun_c_compile_options)

	if(HAS_GCC_BUG_105616 OR HAS_GCC_BUG_105562)
		set_source_files_properties(
			main.cc
			PROPERTIES COMPILE_OPTIONS "-Wno-error=maybe-uninitialized")
	endif()
endif()

#Generates information for test coverage reports.
#If flexisip-conference lib needs to be static, add this rule:
#list(APPEND STATIC_LIBRARY_OPTIONS "-fprofile-instr-generate")
if(ENABLE_COVERAGE)
	if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
		target_compile_options(flexisip-conference PRIVATE "-fprofile-instr-generate")
		target_compile_options(flexisip-conference PRIVATE "-fcoverage-mapping")
		target_link_options(flexisip-conference PRIVATE "-fprofile-instr-generate")
	else()
		message(FATAL "CMAKE_CXX_COMPILER_ID is set to ${CMAKE_CXX_COMPILER_ID}. Coverage works only under Clang. Disable ENABLE_COVERAGE option or use Clang.")
	endif()
endif()
if(APPLE)
	target_link_libraries(flexisip-conference PRIVATE Iconv::Iconv)
endif()

if(ENABLE_SYSTEMD)
	add_compile_definitions(ENABLE_SYSTEMD)
	target_link_libraries(flexisip-conference PRIVATE LibSystemD)
endif()

target_compile_features(flexisip-conference PRIVATE cxx_auto_type cxx_variadic_macros)
target_link_libraries(flexisip-conference
	PRIVATE
	belr
	ortp
	OpenSSL::SSL
	Threads::Threads
	PUBLIC
	bctoolbox
)
if(ENABLE_UNIT_TESTS)
	target_link_libraries(flexisip-conference PRIVATE bctoolbox-tester)
endif()

add_dependencies(flexisip-conference flexisip-conference-git-version)

if(NOT XercesC_FOUND)
	message(FATAL_ERROR "Presence asked but XercesC not enabled! Please enable it first.")
endif()


target_link_libraries(flexisip-conference
	PRIVATE belle-sip
	PUBLIC liblinphone++
	PUBLIC flexisip
)

#######################################################################################################################
# Build the executables
#######################################################################################################################
add_executable(flexisip_conference_server main.cc)
set_property(TARGET flexisip_conference_server PROPERTY OUTPUT_NAME flexisip-conference)
target_compile_features(flexisip_conference_server PRIVATE cxx_auto_type cxx_variadic_macros)
target_link_libraries(flexisip_conference_server PRIVATE flexisip-conference OpenSSL::Crypto ortp)
install(TARGETS flexisip-conference flexisip_conference_server
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

#######################################################################################################################
# Generate default configuration file for flexisip
#######################################################################################################################
if(NOT CMAKE_CROSSCOMPILING)
	add_custom_target(flexisip_conference_config
		ALL
		COMMAND $<TARGET_FILE:flexisip_conference_server> --dump-all-default > "${CMAKE_BINARY_DIR}/flexisip-conference.conf"
		BYPRODUCTS "${CMAKE_BINARY_DIR}/flexisip-conference.conf"
		COMMENT "Flexisip default configuration file generation"
	)
	set(FLEXISIP_CONFIGURATION_FOLDER_INSTALL_LOCATION "${SYSCONF_INSTALL_DIR}")
	add_dependencies(flexisip_conference_config flexisip_conference_server)
	install(FILES "${CMAKE_BINARY_DIR}/flexisip-conference.conf"
		DESTINATION "${FLEXISIP_CONFIGURATION_FOLDER_INSTALL_LOCATION}"
		PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ
	)

	set(FLEXISIP_CONFIGURATION_FOLDER_INSTALL_LOCATION "${FLEXISIP_CONFIGURATION_FOLDER_INSTALL_LOCATION}" PARENT_SCOPE)
	set(FLEXISIP_CONFIGURATION_FILE_INSTALL_LOCATION "${FLEXISIP_CONFIGURATION_FOLDER_INSTALL_LOCATION}/flexisip-conference.conf" PARENT_SCOPE)
endif()