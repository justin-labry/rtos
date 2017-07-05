workspace "Penguin"
configurations { "Debug", "Release" }

configurations "Debug"
flags { "Symbols" }

configurations "Release"
flags { "Optimize" }

language "C"
warnings "Extra"

project "pncp"
    kind    "StaticLib"

    warnings        'Extra'
    buildoptions {
        '-ffreestanding',
        '-std=gnu99',
        '-Wno-unused-parameter'
    }

    targetdir   '.'

    includedirs {
        "../ext/include",
        "../vnic/include",
        "./include"
    }

    files { "src/**.h", "src/**.c" }
