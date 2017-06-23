package = "pthread"
version = "scm-1"
source = {
    url = "gitrec://github.com/mah0x211/lua-pthread.git"
}
description = {
    summary = "pthread module",
    homepage = "https://github.com/mah0x211/lua-pthread",
    license = "MIT/X11",
    maintainer = "Masatoshi Teruya"
}
dependencies = {
    "lua >= 5.1",
    "luarocks-fetch-gitrec >= 0.2"
}
build = {
    type = "builtin",
    modules = {
        pthread = {
            incdirs = { "deps/lauxhlib" },
            libraries = { "pthread" },
            sources = {
                "src/pthread.c"
            }
        }
    }
}

