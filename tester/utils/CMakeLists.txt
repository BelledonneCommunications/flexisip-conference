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

add_library(flexisip_conference_tester_utils
    server/test-conference-server.cc server/test-conference-server.hh
    conference-builder.cc conference-builder.hh
)

target_include_directories(flexisip_conference_tester_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# C++ 20 adds the support for char8_t which adds a lot of backward compatibility problems.
# See: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1423r3.html
# So for now disable it until we need it.
list(APPEND CXX_BUILD_FLAGS "-fno-char8_t")

target_compile_options(flexisip_conference_tester_utils PRIVATE ${CPP_BUILD_FLAGS} ${CXX_BUILD_FLAGS})
target_link_libraries(flexisip_conference_tester_utils PRIVATE flexisip_tester_utils)