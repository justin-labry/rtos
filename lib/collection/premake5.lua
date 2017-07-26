configurations { 'linux' }

include 'testcollection'

project 'collection'
    kind 'StaticLib'

    build.compileProperty('x86_64')
    build.linkingProperty()
    build.targetPath('..')

    postbuildcommands {
        'make -C testcollection'
    }
