import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.model.ClientOptions
import dev.sxlver.overwolfhook.model.OwhConstants
import dev.sxlver.overwolfhook.kotlin.gameState
import dev.sxlver.overwolfhook.kotlin.read
import kotlin.time.Duration.Companion.milliseconds
import java.nio.charset.StandardCharsets

/** External artifact consumer; its input mapping belongs to owh_state_tests. */
fun main(args: Array<String>) {
    require(args.size == 1)
    val channel = args.single().toLong()
    OverwolfHook.open().use { sdk ->
        val options = ClientOptions().setProcessId(ProcessHandle.current().pid())
        val report = sdk.check(options)
        val hostIdentity = OwhConstants.OWH_CHECK_HOST_IDENTITY
        check((report.checked and hostIdentity) != 0L && (report.passed and hostIdentity) == 0L)
        check(sdk.gameState.sources().any { it.sourceChannel == channel && (it.kinds and OwhConstants.OWH_CHANNEL_INFO) != 0L })
        val state = sdk.gameState.storage(channel).read(100.milliseconds)
        val field = state.fields().single { it.key.copyBytes().contentEquals("unknown_key".toByteArray(StandardCharsets.US_ASCII)) }
        check(field.bytes.copyBytes().contentEquals(byteArrayOf(-1, 0, -128, 66)))
        check(field.producerTicks == 123456789L && field.reservedBytes == 8L)
    }
    println("PASS installed Kotlin artifact, duration bridge, generated state bytes and passive preflight")
}
