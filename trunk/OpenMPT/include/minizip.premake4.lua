
 project "minizip"
  uuid "63AF9025-A6CE-4147-A05D-6E2CCFD3A0D7"
  language "C"
  location "../build/gen"
  objdir "../build/obj/minizip"
  includedirs { "../include/zlib", "../include/zlib/contrib/minizip" }
  files {
   "../include/zlib/contrib/minizip/ioapi.c",
   "../include/zlib/contrib/minizip/iowin32.c",
   "../include/zlib/contrib/minizip/mztools.c",
   "../include/zlib/contrib/minizip/unzip.c",
   "../include/zlib/contrib/minizip/zip.c",
  }
  files {
   "../include/zlib/contrib/minizip/crypt.h",
   "../include/zlib/contrib/minizip/ioapi.h",
   "../include/zlib/contrib/minizip/iowin32.h",
   "../include/zlib/contrib/minizip/mztools.h",
   "../include/zlib/contrib/minizip/unzip.h",
   "../include/zlib/contrib/minizip/zip.h",
  }
  dofile "../build/premake4-defaults-LIB.lua"
  dofile "../build/premake4-defaults-static.lua"
