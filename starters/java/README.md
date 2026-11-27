# Java starter

> **Attach may restart Overwolf.** The sample explicitly uses the default
> `OWH_ATTACH_RESTART` mode. If Overwolf is running, attach requests a normal
> close and waits up to ten seconds for that process to exit before launching
> it again. It never force-kills Overwolf; tray-shutdown routing is not proven
> for every version. If Overwolf is absent, attach starts it. Closing the SDK
> does not stop Overwolf. Read [attach modes](https://SteveOberst.github.io/Overwolf-Hook/reference/contracts/#attach-modes)
> before changing the mode.

Copy this directory, including `gradle/wrapper`, for an independent Java application. Requirements: Windows x64, JDK 25 (`JAVA_HOME`), and the x64 Visual C++ Redistributable. No native compiler is required.

The `sdkVersion` Gradle property in `gradle.properties` selects the SDK release; the build downloads its matching native libraries automatically. Website downloads already contain the documented release version. If you copy the raw repository files, replace the placeholder in `gradle.properties` with a published version first. Change `sdkVersion` to upgrade.
From this directory, run:

```powershell
./gradlew.bat installDist
./build/install/first-overlay-java/bin/first-overlay-java.bat
```

Before running, enable Overwolf's overlay for a compatible game. The sample submits an opaque orange panel and waits for Enter before closing; submission does not confirm display. Restart attach starts Overwolf if it is absent and may close and relaunch it if already running. See the [installation guide](https://SteveOberst.github.io/Overwolf-Hook/getting-started/installation/) and [first-overlay task](https://SteveOberst.github.io/Overwolf-Hook/overlays/first-overlay/).

Application code is MIT licensed. Gradle wrapper notices are included in `GRADLE-LICENSE.txt` and `GRADLE-NOTICE.txt`.
