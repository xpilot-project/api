# xPilot API
API to access xPilot's aircraft information.
Data transfer from xPilot to your plugin is by dataRefs in a fast, efficient way: xPilot copies data of several planes combined into defined structures. XPilot-API handles all that in the background and provides you with an array of aircraft information with numerical info like position, heading, speed and textual info like type, registration, call sign, flight number.

The xPilot API is derived from [TwinFan's LTAPI](https://github.com/TwinFan/LTAPI). Thank you for your contributions to the flight simulation community.

## XPilot-API files

You only need to include 2 files into your own projects:
- `XPilotAPI.cpp`
- `XPilotAPI.h`
