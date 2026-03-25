############################################################################
# CMakeLists.txt
# Copyright (C) 2017-2026 Belledonne Communications, Grenoble France
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

# logrotate conf
install(FILES flexisip-conference-logrotate DESTINATION "${LOGROTATE_DIR}")
set(FLEXISIP_LOGROTATE_INSTALL_LOCATION "${LOGROTATE_DIR}/flexisip-conference-logrotate" PARENT_SCOPE)

set(bindir ${CMAKE_INSTALL_FULL_BINDIR})

# systemd
set(systemd_services "conference")

# service configuration files
foreach(service ${systemd_services})
    set(after "")

    set(description "Flexisip conference server")
    set(after " redis.service mysqld.service mariadb.service")

    set(service_description "${description}")
    set(service_after "${after}")
    set(service_execstart "${bindir}/flexisip-conference --disable-stdout --syslog --pidfile /var/run/flexisip-${service}.pid")
    set(service_filename "flexisip-${service}.service")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/flexisip.service.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${service_filename}"
    )
    list(APPEND generated_services "${CMAKE_CURRENT_BINARY_DIR}/${service_filename}")

    # template file
    set(service_description  "${description} - %i instance")
    set(service_after "${after}")
    set(service_execstart "/opt/belledonne-communications/flexisip-conference/bin/flexisip-conference --disable-stdout --syslog --pidfile /var/run/flexisip-${service}/%i.pid -c /etc/opt/belledonne-communications/flexisip-conference/%i.conf")
    set(service_filename_at "flexisip-${service}@.service")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/flexisip.service.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${service_filename_at}"
    )
    list(APPEND generated_services "${CMAKE_CURRENT_BINARY_DIR}/${service_filename_at}")
endforeach()

install(FILES ${generated_services} DESTINATION "${FLEXISIP_SYSTEMD_INSTALL_DIR}")