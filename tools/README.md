# Maintainer tools

Build with `-DOWH_BUILD_TOOLS=ON`. These executables are not installed or distributed as public SDK features; commands and recording formats may change.

The `examples/` subdirectory also holds maintainer-only C/Java/Kotlin host-report
probes. They are not part of the website's public examples or consumer smoke.

- `owh_info list` lists provider channels; `read <source-channel>` reads current state.
- `owh_info watch <seconds|0> <new JSONL file> [--stop-file <path>]` records observed state updates.
- `owh_controller <Overwolf PID> <seconds|0> <new capture file> [all|source-channel] [--stop-file path] [--ready-file path]` captures events.
- `owh_controller stream <Overwolf PID> [all|source-channel] [--stop-file path] [--ready-file path]` prints events.

Use only a deliberately selected Overwolf test session. `0` means run until explicitly stopped. Recording outputs must be new files. `tests/controller_cli_test.py` and `tests/info_recorder_test.py` exercise the stop/recording contracts against owned fixtures.
