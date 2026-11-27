pluginManagement {
    repositories { gradlePluginPortal(); mavenCentral() }
}
dependencyResolutionManagement {
    repositories { mavenCentral() }
}
rootProject.name = "overwolf-hook"

include("java", "kotlin")
