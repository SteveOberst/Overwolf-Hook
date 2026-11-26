# SDK consumer fixtures

These are verification programs, not user examples or project templates.

- `native/` imports an installed CMake package, compiles the public C/C++ headers,
  stages the Client/Agent pair, and checks ABI and wrong-process rejection.
- `kotlin/` resolves published-format Ivy artifacts from an explicit local
  `sdkRepository` at `sdkVersion`. The root JVM wrapper can build it. Its input
  channel is provided by `owh_state_tests`; it checks generated state bytes,
  the Kotlin duration overload and passive preflight.
- `java/` independently resolves the installed Java Ivy artifact and exercises
  context creation, generated options, context-free passive discovery and an owned state
  snapshot from the same synthetic InfoDB fixture.

They have no dependency on the SDK's source sets and do not open a game or live
Overwolf process. CI owns installation, artifact staging and fixture invocation.
Direct ABI-level Java checks live in `tests/ffm/java`; packaged Java checks live
in the JVM test source set.
