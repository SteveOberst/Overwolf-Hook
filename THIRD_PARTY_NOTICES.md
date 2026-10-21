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

Overwolf Hook is an independent project and is not affiliated with or endorsed by
Overwolf or Riot Games.
