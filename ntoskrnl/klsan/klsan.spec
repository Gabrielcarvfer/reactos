# Memory leak sanitizer (ntoskrnl/klsan/klsan.c) for tests and tools:
# appended to ntoskrnl.spec in ENABLE_KLSAN builds only
@ stdcall KlsanControl(ptr ptr long ptr)
