plugins {
    application
}

dependencies {
    implementation(project(":grlibre-api"))
    implementation(libs.grpc.netty.shaded)
    implementation(libs.grpc.services)
    implementation(libs.jodconverter.local)
    implementation(libs.log4j.core)

    testImplementation(libs.junit.jupiter)
    testImplementation(libs.grpc.inprocess)
    testRuntimeOnly(libs.junit.launcher)
}

application {
    mainClass.set("ai.pipestream.grlibre.server.GrLibreServer")
}

tasks.test {
    // The real-conversion integration tests find soffice on PATH themselves
    // and skip when it is absent; no flag needed.
    systemProperty("java.io.tmpdir", System.getProperty("java.io.tmpdir"))
}
