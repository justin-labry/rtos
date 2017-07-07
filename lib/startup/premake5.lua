project 'startup'
    kind    "StaticLib"

    build.compileProperty('x86_64')
    build.targetPath('..')

    includedirs {
        "../ext/include",
        "../vnic/include",
        "./include"
    }

    files { "src/**.h", "src/**.c" }
