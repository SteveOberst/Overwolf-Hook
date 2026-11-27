package dev.sxlver.overwolfhook.kotlin

import kotlin.time.Duration
import kotlin.test.assertNull
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.microseconds

class FacadeTest {
    @Test fun finiteDurationsMustBeExactNativeMilliseconds() {
        assertEquals(java.time.Duration.ofMillis(123), 123.milliseconds.wholeJavaDuration())
        assertFailsWith<IllegalArgumentException> { 1.microseconds.wholeJavaDuration() }
    }
    @Test fun infiniteWaitsUseTheExplicitJavaForm() {
        assertNull(Duration.INFINITE.optionalJavaTimeout())
        assertEquals(java.time.Duration.ZERO, Duration.ZERO.optionalJavaTimeout())
    }
}
