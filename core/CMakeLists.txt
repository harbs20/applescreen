# Ad-hoc signing after every build, not a manual script step - Apple Silicon
# refuses to load an unsigned dylib at all (even injected ones), so this
# can't be something that's skippable/forgettable.
function(applescreen_sign target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_FILE:${target}>
        COMMENT "Ad-hoc signing ${target} (required to load on Apple Silicon)"
    )
endfunction()

# M0 spike: proves injection + dlsym-interpose observation works at all,
# before any real hook logic is written against that assumption. Deliberately
# has zero shared code with the real core below.
add_library(applescreen_spike SHARED spike/spike.c)
set_target_properties(applescreen_spike PROPERTIES
    OUTPUT_NAME "applescreen_spike"
    SUFFIX ".dylib"
)
target_link_options(applescreen_spike PRIVATE -Wl,-ld_classic)
applescreen_sign(applescreen_spike)

# The real injected core.
add_library(applescreen_core SHARED
    src/init.c
    src/log.c
    src/interpose.c
    src/glfw_shim.c
    src/window_control.c
    src/input_shim.c
    src/overlay_gl2.c
    src/config.c
    src/modes.c
    src/hotkeys.c
    src/key_rebind.c
)
set_target_properties(applescreen_core PROPERTIES
    OUTPUT_NAME "applescreen_core"
    SUFFIX ".dylib"
)
target_link_libraries(applescreen_core PRIVATE pthread "-framework OpenGL")
target_link_options(applescreen_core PRIVATE -Wl,-ld_classic)
applescreen_sign(applescreen_core)
