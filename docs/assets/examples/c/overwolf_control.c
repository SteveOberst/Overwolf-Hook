#include <overwolf_hook/abi/overwolf.h>
#include <stdio.h>

// --8<-- [start:installation]
void show_installation(void) {
    owh_overwolf_installation installation = {0};
    installation.size = sizeof(installation);
    owh_result result = owh_overwolf_installation_query(&installation);
    if (result.status == OWH_OK) {
        printf("Directory: %s\nLauncher: %s\nVersion: %s\n",
               installation.directory, installation.launcher,
               installation.version);
    } else if (result.status == OWH_NOT_FOUND) {
        puts("Overwolf installation not found.");
    } else {
        fprintf(stderr,
                "Installation query failed: status=%d stage=%u native=%llu\n",
                result.status, result.failure_stage,
                (unsigned long long)result.native_code);
    }
}
// --8<-- [end:installation]

// --8<-- [start:process]
void show_process(void) {
    owh_overwolf_process process = {0};
    process.size = sizeof(process);
    owh_result result = owh_overwolf_process_query(0, 0, &process);
    if (result.status == OWH_OK) {
        printf("PID=%u\nExecutable=%s\nVersion=%s\n", process.process_id,
               process.executable, process.version);
    } else if (result.status == OWH_NOT_FOUND) {
        puts("Overwolf is not running.");
    } else {
        fprintf(stderr, "Process query failed: status=%d stage=%u\n",
                result.status, result.failure_stage);
    }
}
// --8<-- [end:process]

// --8<-- [start:start]
void start_overwolf(void) {
    owh_overwolf_process process = {0};
    process.size = sizeof(process);
    owh_result result = owh_overwolf_start(10000, &process);
    if (result.status == OWH_OK) {
        printf("Overwolf is running: PID=%u\n", process.process_id);
    } else {
        fprintf(stderr, "Start failed: status=%d stage=%u native=%llu\n",
                result.status, result.failure_stage,
                (unsigned long long)result.native_code);
    }
}
// --8<-- [end:start]

// --8<-- [start:stop]
void stop_overwolf(void) {
    owh_overwolf_process process = {0};
    process.size = sizeof(process);
    owh_result result = owh_overwolf_process_query(0, 0, &process);
    if (result.status == OWH_OK) {
        result = owh_overwolf_stop(process.process_id, process.process_started,
                                   10000);
    }
    if (result.status == OWH_NOT_FOUND) {
        puts("Overwolf is not running.");
    } else if (result.status != OWH_OK) {
        fprintf(stderr, "Stop failed: status=%d stage=%u native=%llu\n",
                result.status, result.failure_stage,
                (unsigned long long)result.native_code);
    }
}
// --8<-- [end:stop]
