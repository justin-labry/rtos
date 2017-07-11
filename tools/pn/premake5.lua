project "pn"
    language 'C'
    kind 	"ConsoleApp"
    targetdir	"."
    targetname	"pnd"

    buildoptions {
        "-idirafter ../../../kernel/src",
        "-std=gnu99",
        "-mcmodel=large",
        "-Wno-unused",
        "-Wno-format",
        "-Wno-unused-result",
        "-fno-builtin"
    }
    linkoptions { "-nostartfiles", "-Wl,-Ttext-segment=0xff00000000" }
    includedirs { "../../lib/include", "." }
    libdirs {
        "../../lib/ext",
        "../../lib/vnic",
        "../../lib/tlsf"
    }
    links { "ext", "rt", "vnic", "tlsf" }
    build.compileProperty('x86_64')
    location	"build"

    -- Make version header
    prebuildcommands {
        '../../../scripts/mkver.sh > ../src/version.h',
    }

project 'pn_build'
    kind        'Makefile'
    location    '.'

    buildcommands {
        'test -f ./packetngin-boot.param || ./kpart',
        'make -C drivers',
        'make -C build'
    }

    cleancommands {
        'make clean -C drivers',
        'make clean -C build'
    }
