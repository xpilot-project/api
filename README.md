# xPilot API
API to access xPilot's aircraft information.
Data transfer from xPilot to your plugin is by dataRefs in a fast, efficient way: xPilot copies data of several planes combined into defined structures. XPilot-API handles all that in the background and provides you with an array of aircraft information with numerical info like position, heading, speed and textual info like type, registration, call sign, flight number.

## How to use
You only need to include 2 files into your own projects:
- `XPilotAPI.cpp`
- `XPilotAPI.h`

For examples on how to implement the API, see [LTAPI](https://github.com/TwinFan/LTAPI) for more information. See the `XPilotAPIBulkData` and `XPilotAPIBulkInfoTexts` structs in [XPilotAPI.h](XPilotAPI.h) for details on what information is available for consumption.

## License
MIT License, see [LICENSE.md](LICENSE.md).

## Acknowledgements
The xPilot API is derived from [TwinFan's LTAPI](https://github.com/TwinFan/LTAPI).
