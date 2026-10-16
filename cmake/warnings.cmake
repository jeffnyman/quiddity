# Warning policy shared by every target in the project.
#
# The policy is deliberately strict. An interpreter spends its life doing
# narrow integer arithmetic on bytes and 16-bit words, which is exactly the
# territory where implicit conversions hide bugs. Making every narrowing
# conversion explicit costs a cast at the site; it buys a compiler that
# complains the moment a 32-bit intermediate leaks into a 16-bit result.
#
# Usage:
#
#     quiddity_apply_warnings(<target>)
#
# Applies the flags as PRIVATE compile options, so they govern how the
# target is built without leaking into anything that links against it.

function(quiddity_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4             # Highest practical warning level.
            /WX             # Warnings are errors.
            /permissive-    # Standards-conforming front end.
            /utf-8          # Source and execution character sets are UTF-8.
        )
        # The MSVC C runtime flags standard functions such as fopen and
        # strcpy as "unsafe" and suggests Microsoft-only replacements. The
        # project uses the standard functions correctly and must stay
        # portable, so that advice is switched off rather than followed.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wshadow            # An inner name hiding an outer one.
            -Wconversion        # Implicit conversions that may change a value.
            -Wsign-conversion   # Implicit signed/unsigned conversions.
            -Wdouble-promotion  # float silently widened to double.
            -Wcast-qual         # Casting away const.
            -Wstrict-prototypes # f() must be written f(void).
            -Wundef             # #if on an undefined macro.
        )
    endif()

    if(QUIDDITY_SANITIZE)
        if(MSVC)
            message(WARNING "QUIDDITY_SANITIZE is only supported with GCC and Clang; ignoring.")
        else()
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE
                -fsanitize=address,undefined
            )
        endif()
    endif()
endfunction()
