package dev.sxlver.overwolfhook.kotlin

import kotlin.time.Duration
import kotlin.test.assertNull
import dev.sxlver.overwolfhook.model.EventOptions
import dev.sxlver.overwolfhook.model.OverlaySpec
import dev.sxlver.overwolfhook.model.PixelRegion
import dev.sxlver.overwolfhook.model.WaitOptions
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.microseconds

class FacadeTest {
    @Test fun generatedModelsAreTheKotlinInputAndOutputTypes() {
        EventOptions().setSourceChannel(42)
        OverlaySpec().setWidth(320).setHeight(180)
        PixelRegion().setX(16).setY(32).setWidth(224).setHeight(64)
        WaitOptions().setTimeoutMs(100)
    }

    @Test fun finiteDurationsMustBeExactNativeMilliseconds() {
        assertEquals(java.time.Duration.ofMillis(123), 123.milliseconds.wholeJavaDuration())
        assertFailsWith<IllegalArgumentException> { 1.microseconds.wholeJavaDuration() }
    }
    @Test fun infiniteWaitsUseTheExplicitJavaForm() {
        assertNull(Duration.INFINITE.optionalJavaTimeout())
        assertEquals(java.time.Duration.ZERO, Duration.ZERO.optionalJavaTimeout())
    }
}
