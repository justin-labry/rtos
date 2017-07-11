-- [[ Premake script for kernel workspace ]] 
workspace "Kernel"
    -- [[ 1. Linux compability layer ]]
    project "linux"
        language 'C'
        kind "ConsoleApp"
        location "linux/build"
        targetname "linux.ko"
        targetdir "."
        includedirs { "../kernel/src", "../lib/core/include", "./linux" }
        linkoptions { "-r" }
        files { "linux/**.h", "linux/**.c" }
        removefiles { "linux/packetngin/**.h", "linux/packetngin/**.c" }

    -- [[ 2. VirtI/O ]]
    project "virtio"
        language 'C'
        kind "ConsoleApp"
        location "virtio/build"
        targetname "virtio.ko"
        targetdir "."
        includedirs { "../kernel/src", "../kernel/src/driver", "../lib/vnic/include", "../lib/core/include" }
        files { "virtio/**.h", "virtio/**.c" }
        linkoptions { "-r ../../linux.ko" }

    -- [[ 3. FAT ]]
    project "fat"
        language 'C'
        kind "ConsoleApp"
        location "fat/build"
        targetname "fat.ko"
        targetdir "."
        includedirs { "../kernel/src", "../kernel/src/driver", "../lib/vnic/include", "../lib/core/include" }
        files { "virtio/**.h", "virtio/**.c" }
        files { "fat/**.h", "fat/**.c" }
        linkoptions { "-r ../../linux.ko" }
