include 'console'
include 'pn'

project 'tools'
    kind        'Makefile'
    location    '.'

    buildcommands {
        'make -C console',
        'make -C pn',
        'make -C pnkc',
        'make -C smap',
    }

    cleancommands {
        'make clean -C console',
        'make clean -C pn',
        'make clean -C pnkc',
        'make clean -C smap',
    }
