# support creating some basic binpkgs via `make package`

set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR TGZ)

include(CPack)
