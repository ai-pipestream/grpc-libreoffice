subprojects {
    group = "ai.pipestream.grlibre"
    version = "0.1.0-SNAPSHOT"

    plugins.withType<JavaPlugin> {
        extensions.configure<JavaPluginExtension> {
            toolchain {
                languageVersion.set(JavaLanguageVersion.of(25))
            }
        }
        tasks.withType<Test>().configureEach {
            useJUnitPlatform()
        }
    }
}
