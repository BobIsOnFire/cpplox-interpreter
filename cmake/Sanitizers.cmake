option(ENABLE_ADDRESS_SANITIZER "Enable LLVM AddressSanitizer" OFF)
option(ENABLE_LEAK_SANITIZER "Enable LLVM LeakSanitizer" OFF)
option(ENABLE_MEMORY_SANITIZER "Enable LLVM MemorySanitizer" OFF)
option(ENABLE_THREAD_SANITIZER "Enable LLVM ThreadSanitizer" OFF)
option(ENABLE_UNDEFINED_BEHAVIOUR_SANITIZER "Enable LLVM UndefinedBehaviourSanitizer" OFF)

set(sanitizer_flags)
if(ENABLE_ADDRESS_SANITIZER)
    list(APPEND sanitizer_flags address)
endif()
if(ENABLE_LEAK_SANITIZER)
    list(APPEND sanitizer_flags leak)
endif()
if(ENABLE_MEMORY_SANITIZER)
    list(APPEND sanitizer_flags memory)
endif()
if(ENABLE_THREAD_SANITIZER)
    list(APPEND sanitizer_flags thread)
endif()
if(ENABLE_UNDEFINED_BEHAVIOUR_SANITIZER)
    list(APPEND sanitizer_flags undefined)
endif()

if(sanitizer_flags)
    list(JOIN sanitizer_flags "," sanitizer_flags_str)

    add_compile_options(
        $<$<CONFIG:DEBUG>:-fsanitize=${sanitizer_flags_str}>
        $<$<CONFIG:DEBUG>:-fno-optimize-sibling-calls>
        $<$<CONFIG:DEBUG>:-fno-omit-frame-pointer>
        $<$<CONFIG:DEBUG>:-O1>
    )
    add_link_options(
        $<$<CONFIG:DEBUG>:-fsanitize=${sanitizer_flags_str}>
    )
endif()
