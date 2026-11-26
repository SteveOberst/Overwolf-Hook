# Third-party notices

Overwolf Hook's original code is licensed under MIT; see [LICENSE](LICENSE).

The resident Agent statically links [MinHook 1.3.4](https://github.com/TsudaKageyu/minhook/tree/v1.3.4),
licensed under BSD-2-Clause. Owned hook tests also use
MinHook. Its instruction decoder carries notices included in the upstream
license file. The complete text is preserved in the repository under
third_party/MinHook.LICENSE.txt and distributed with binary packages under
share/doc/overwolf_hook/third-party/MinHook-LICENSE.txt.
JVM artifacts also include the corresponding notices.

Client and Agent use the header-only
[Boost.Interprocess 1.92.0](https://www.boost.org/library/1.92.0/interprocess/)
and its Boost header dependencies under the Boost Software License 1.0. The
complete license is in third_party/Boost.LICENSE.txt and distributed as
share/doc/overwolf_hook/third-party/Boost-LICENSE.txt. No Boost runtime DLL is
required. The official source archive is fetched with a pinned SHA-256.

MinHook's source archive is fetched with a pinned SHA-256. No Overwolf or Riot
binaries, SDK headers, decompiler output, live recordings or game assets are
included. Optional consumer tests use a locally supplied Overwolf DLL.

The JVM build uses [OpenJDK jextract](https://github.com/openjdk/jextract) to
generate FFM bindings from this project's public headers. Its tool/runtime is
not bundled in library artifacts. The download URL and checksum are configured
in jvm/tools/jextract.json.

The JVM build uses the [Gradle Wrapper](https://github.com/gradle/gradle), licensed
under Apache-2.0. Its scripts retain upstream copyright and license notices.
The wrapper targets Gradle 9.3.0; its distribution checksum is pinned in each
`gradle-wrapper.properties`. The upstream license and notice are preserved in
[third_party/Gradle.LICENSE.txt](https://github.com/SteveOberst/Overwolf-Hook/blob/main/third_party/Gradle.LICENSE.txt) and
[third_party/Gradle.NOTICE.txt](https://github.com/SteveOberst/Overwolf-Hook/blob/main/third_party/Gradle.NOTICE.txt). Standalone JVM
starters carry their own copies so the downloaded projects retain those notices.
The Kotlin API depends on [Kotlin](https://github.com/JetBrains/kotlin) and
[kotlinx.coroutines](https://github.com/Kotlin/kotlinx.coroutines), licensed under
Apache-2.0. These are normal Gradle dependencies, not shaded into the core Java
SDK or native binaries.

Overwolf Hook is independent of and not endorsed by Overwolf or Riot Games.
