# Third-party notices

Overwolf Hook's original code is licensed under MIT; see [LICENSE](LICENSE).

When the event hook is enabled, the capture DLL statically links [MinHook 1.3.4](https://github.com/TsudaKageyu/minhook/tree/v1.3.4),
licensed under BSD-2-Clause. Its bundled instruction decoder also carries notices
included in the upstream license file. The complete upstream text is preserved
in [third_party/MinHook.LICENSE.txt](third_party/MinHook.LICENSE.txt) and in binary
packages under `share/doc/overwolf_hook/third-party/MinHook-LICENSE.txt`.

MinHook's source archive is fetched by CMake with a pinned SHA-256. No Overwolf or
Riot binaries, SDK headers, decompiler output, live recordings or game assets are
included. The optional consumer test requires a locally supplied Overwolf DLL.

The optional JVM build uses [OpenJDK jextract](https://github.com/openjdk/jextract)
to generate FFM bindings from this project's public headers. Its downloaded build
tool/runtime is not bundled in the library artifacts. Generator URL and SHA-256
are pinned in [jvm/java/toolchain.json](jvm/java/toolchain.json). The Kotlin example uses the
[Gradle Wrapper](https://github.com/gradle/gradle), licensed under Apache-2.0; its
scripts retain the upstream copyright and license notices.

The optional Kotlin API depends on [Kotlin](https://github.com/JetBrains/kotlin)
and [kotlinx.coroutines](https://github.com/Kotlin/kotlinx.coroutines), licensed
under Apache-2.0. These are normal Gradle dependencies and are not shaded into
the Java SDK or native binaries.

Overwolf Hook is an independent project and is not affiliated with or endorsed by
Overwolf or Riot Games.
