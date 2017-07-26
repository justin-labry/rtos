project 'testcollection'
    kind 'ConsoleApp'
    language 'C'
	location '.'

    includedirs "../include"
	files { "src/**.h", "src/**.c" }
    links { 'collection' }
	postbuildcommands { "%{cfg.buildtarget.abspath}" } -- run tester
    buildoptions { '-std=gnu11' }
