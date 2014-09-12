package = "pthread"
version = "scm-1"
source = {
    url = "git://github.com/mah0x211/lua-pthread.git"
}
description = {
    summary = "pthread module",
    homepage = "https://github.com/mah0x211/lua-pthread",
    license = "MIT/X11",
    maintainer = "Masatoshi Teruya"
}
dependencies = {
    "lua >= 5.1"
}
build = {
    type = "builtin",
    modules = {
        pthread = {
            sources = { "pthread.c" },
        }
    }
}

