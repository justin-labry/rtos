project "connect"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "connect"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/connect.c" }

project "create"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "create"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/create.c" }

project "destroy"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "destroy"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/destroy.c" }

project "list"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "list"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/list.c" }

project "status"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "status"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/status.c" }

project "start"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "start"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/start.c" }

    postbuildcommands {
        "cp ../bin/start ../bin/pause",
        "cp ../bin/start ../bin/resume",
        "cp ../bin/start ../bin/stop",
    }
    -- FIXME: Start, Pause, Resume, Stop commands are same. Need to be refactored

project "upload"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "upload"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/upload.c" }

project "download"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "download"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/download.c" }

project "monitor"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "monitor"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/monitor.c" }

project "stdin"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "stdin"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/stdin.c" }

project "md5"
    language 'C'
    includedirs { "../../lib/include", "include" }
    libdirs { "../../lib/ext/", "../../lib/tlsf/", "../../lib/hal/" }
    linkoptions { "-lc" }
    links { "ext", "tlsf", "hal" }
    location "build"
    kind "ConsoleApp"
    targetdir "bin"
    targetname "md5"
    buildoptions { "-std=gnu99"}
    files { "src/rpc.c" }
    files { "src/md5.c" }

project 'console'
    language 'C'
    kind        'Makefile'
    location    '.'

    buildcommands {
        'make -C build -f connect.make',
        'make -C build -f create.make',
        'make -C build -f destroy.make',
        'make -C build -f list.make',
        'make -C build -f status.make',
        'make -C build -f start.make',
        'make -C build -f upload.make',
        'make -C build -f download.make',
        'make -C build -f monitor.make',
        'make -C build -f stdin.make',
        'make -C build -f md5.make',
    }

    cleancommands {
        'make -C build clean -f connect.make clean',
        'make -C build clean -f create.make clean',
        'make -C build clean -f destroy.make clean',
        'make -C build clean -f list.make clean',
        'make -C build clean -f status.make clean',
        'make -C build clean -f start.make clean',
        'make -C build clean -f upload.make clean',
        'make -C build clean -f download.make clean',
        'make -C build clean -f monitor.make clean',
        'make -C build clean -f stdin.make clean',
        'make -C build clean -f md5.make clean',
        "rm -f bin/pause",
        "rm -f bin/resume",
        "rm -f bin/stop",
    }
