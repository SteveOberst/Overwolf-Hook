# Constants and flags

Look up status codes, failure stages, feature flags, queue policies, and resource
states. The names and numeric values come from the public C headers. Java and
Kotlin use the same names through generated `OwhConstants`.

## Values and meanings

Use these names from the C headers or generated `OwhConstants` in Java/Kotlin.
Values are extracted from the headers when the site builds. Bitmasks (`checked`,
`passed`, `warnings`, `available`, `enabled`, `kinds` and feature flags) can
contain multiple bits; states and statuses are single values. A failure stage
identifies where an operation failed, not another status. The `OWH_*_INIT`
macros initialize C input/output storage; prefer the init function for inputs
and `sizeof(output)` for size-tagged outputs.

--8<-- "docs/assets/generated/reference/constants.txt"
