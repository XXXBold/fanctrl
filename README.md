# fanctrl
Application to control fanspeed on Linux Operating System

## Build & Install
Build using the makefile (is configured for gcc), run "make CFG=Release".
This project doesn't rely on other dependencies, simply build and use.
- Install by copying the executable to the location of your desire, e.g. /usr/local/bin/.
- Create configuration file (Use fanctrl_config.txt as template). Place in desired directory, e.g. /etc/.

## How to use
The Application must be started with a configuration file, containing paths and fancontrol informations.
While testing, start manually "fanctrl /path/to/cfgfile --debug" and verify everything works properly.
When satisfied with the results, create a systemd script to run automatically on startup.

## CLI
- Start Application: application 'path-to-config-file' [--debug]
- Help: application --help
- Get Current Version: application --version

## Start automatically as Systemd-unit
fanctrl.service is an example script for systemd integration. Use & modify as you need.

## Currently implemented Fans
- AMDGPU

## Versions
- v0.1.0 (beta): First Release

## Support
If you encounter problems or have suggestions for improvements, feel free to open an issue.
