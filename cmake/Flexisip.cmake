############################################################################
# LinphoneSDK.cmake
# Copyright (C) 2026 Belledonne Communications, Grenoble France
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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
############################################################################

############################################################################
# Add Flexisip project as subproject
############################################################################

function(add_flexisip)
	set(BUILD_AS_LIB ON)
	set(ENABLE_REDIS ON)
	set(ENABLE_SOCI ON)
	set(ENABLE_TRANSCODER OFF)
	set(ENABLE_OPENID_CONNECT OFF)
	set(ENABLE_EXTERNAL_AUTH_PLUGIN OFF)
	set(ENABLE_UNIT_TESTS_NGHTTP2ASIO ${ENABLE_UNIT_TESTS})

	set(ENABLE_FLEXIAPI ${ENABLE_UNIT_TESTS})
	set(INTERNAL_LIBSRTP2 OFF)

	set(ENABLE_B2BUA OFF)
	set(ENABLE_PRESENCE OFF)
	set(ENABLE_REGEVENT ON)
	set(ENABLE_VOICEMAIL OFF)
	set(ENABLE_CONFERENCE OFF)
	set(FLEXISIP_VERSION ${FLEXISIP_CONFERENCE_FULL_VERSION})

	add_subdirectory("flexisip")
endfunction()

add_flexisip()
