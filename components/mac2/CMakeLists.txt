# Macintosh LC (platform independent; host build in host/)
idf_component_register(SRCS "mac.c"
                            "mem.c"
                            "via.c"
                            "pvia.c"
                            "egret.c"
                            "adb.c"
                            "input.c"
                            "video.c"
                            "asc.c"
                            "chips.c"
                            "sony.c"
                            "rom.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES musashi)

# Runs together with the CPU core in the emulation loop
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
