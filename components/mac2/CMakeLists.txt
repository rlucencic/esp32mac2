# Macintosh LC and II (platform independent; host build in host/)
idf_component_register(SRCS "mac.c"
                            "mem.c"
                            "via.c"
                            "lc.c"
                            "pvia.c"
                            "egret.c"
                            "video.c"
                            "ii.c"
                            "jmfb.c"
                            "adb_via.c"
                            "rtc.c"
                            "adb.c"
                            "input.c"
                            "asc.c"
                            "chips.c"
                            "sony.c"
                            "rom.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES musashi
                       PRIV_REQUIRES diskspace)

# Runs together with the CPU core in the emulation loop
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
