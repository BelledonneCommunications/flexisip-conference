# Change Log

All notable changes to this project will be documented in this file.

Group changes to describe their impact on the project, as follows:

| **Group name** | **Description**                                                             |
|----------------|-----------------------------------------------------------------------------|
| Added          | New features                                                                |
| Changed        | Changes in existing functionality                                           |
| Deprecated     | Once-stable features removed in upcoming releases                           |
| Removed        | Deprecated features removed in this release                                 |
| Fixed          | Any bug fixes                                                               |
| Security       | To invite users to upgrade in case of vulnerabilities                       |
| Known Issues   | Issues whose fix has not been tested and cannot be included in this release |

## [1.1.0] - Alpha

## [1.0.0] - Beta
The following changes are made in comparison to the conference server released in Flexisip 2.5.1.

### [Added]
- **Conference server:**
    - Add the 'no-rtp-timeout' parameter that allows to set the delay before the call is automatically hung up because
      no RTP data is received.
    - Add parameter 'cleanup-expired-conferences' to enable periodic removal of expired conferences.
    - Add parameter 'conferences-availability-before-start' to set how long before the start time of a conference it is
      possible to join it.
    - Add parameter 'conferences-expiry-time' to set how long after the end of a conference it is still possible to
      join it.
    - Add support for end-to-end conference encryption by installing the EKT server plugin. For customers under a
      proprietary license, this functionality is under a specific license.
- **Build:**
    - Add support for C++20.
    - Add support for Ubuntu 26.04.

- **Global:** Parameter `global/watchdog-notify-interval` set the interval between notifications of a flexisip service
    to the watchdog of SystemD.
- **Command line:** Option `--disable-stdout` in command line to not display the log in standard output.


### [Changed]
- **Service:** The Flexisip services are now managed and started directly by SystemD. Service behavior is configured in
    the SystemD unit files. By default, a service notifies SystemD once startup is complete (using `Type=notify`). The
    SystemD watchdog is then enabled, and the service's main loop periodically notifies the watchdog to confirm that the
    process is alive and not blocked. The watchdog timeout is configured in the service's unit file by `WatchdogSec`, 
    while the main loop notification interval is set via the global parameter `watchdog-notify-interval`. Flexisip now
    uses `Restart=on-failure` in its SystemD service units. This ensures that if a service crashes, exits with a
    non-zero status or fails to notify the watchdog before the timeout, SystemD automatically restarts it.

### [Deprecated]
- **Global:** Parameter `global/auto-respawn` no longer has any effect.

### [Fixed]
- **Redis client:**
    - Wait 1 second before retrying to connect when the connection to the server fails instead of retrying immediately.

### [Removed]
- **Conference:**
    - Parameter `conference-server/conference-factory-uri` (deprecated in 2.1.0)
    - Parameter `conference-server/conference-factory-uri` (deprecated in Flexisip 2.1.0)
- **Configuration:** Parameters of type `StringList` can no longer be declared using the character `','` to separate
  each element.
- **Debian 11:** Support discontinued, as distribution will reach its end-of-life (2026-08-31).
- **Ubuntu 22.04:** Support discontinued
- **Global:**
    - Option `--daemon` in flexisip services start command
    - Parameter `global/tls-certificates-dir` (deprecated in Flexisip 2.2.0)
    - `tls-certificates-dir` was removed from transports parameters (deprecated in 2.2.0)
- **Registrar:** 
    - Unused `ctdumper` and `serializer` tools.
    - MSGPACK feature (deprecated in Flexisip 2.4.0).
- **All servers:** The launcher and watchdog processus are replaced by the SystemD startup with `Type=notify` and its watchdog.
