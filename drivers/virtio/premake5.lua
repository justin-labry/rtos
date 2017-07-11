project 'virtio'
    language 'C'
    kind        'ConsoleApp'
    targetname  'virtio.ko'

    buildoptions { '-mcmodel=kernel' }
    build.compileProperty('x86_64')
    build.linkingProperty{}
    build.targetPath('..')

    linkoptions { '-r ../linux.ko' }
    includedirs { '../../kernel/src', '../../lib/include' }
