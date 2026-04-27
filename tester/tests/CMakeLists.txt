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

add_executable(flexisip_conference_tester
	conference/conference-server-tester.cc
	conference/registration-subscription-tester.cc
	main-tester.cc

	${PROJECT_SOURCE_DIR}/flexisip/tester/tests/tester.cc ${PROJECT_SOURCE_DIR}/flexisip/tester/tests/tester.hh
)

if(ENABLE_UNIT_TESTS_NGHTTP2ASIO)
	target_sources(flexisip_conference_tester PRIVATE
		eventlogs/writers/flexi-stats-event-log-writer-tester.cc
	)
endif()

if(HAS_GCC_BUG_105562)
	set_source_files_properties(
		eventlogs/writers/flexi-stats-event-log-writer-tester.cc
		PROPERTIES COMPILE_OPTIONS "-Wno-error=maybe-uninitialized"
	)
endif()

# C++ 20 adds the support for char8_t which adds a lot of backward compatibility problems.
# See: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1423r3.html
# So for now disable it until we need it.
list(APPEND CXX_BUILD_FLAGS "-fno-char8_t")

target_compile_options(flexisip_conference_tester PRIVATE ${CPP_BUILD_FLAGS} ${CXX_BUILD_FLAGS})
target_link_libraries(flexisip_conference_tester PRIVATE
	OpenSSL::SSL
	flexisip
	flexisip-conference
	flexisip_tester_utils
	flexisip_conference_tester_utils
)