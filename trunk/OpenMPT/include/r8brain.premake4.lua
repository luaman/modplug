  
 project "r8brain"
  uuid "BC116B29-9958-4389-B294-7529BB7C7D37"
  language "C++"
  location "../build/gen"
  objdir "../build/obj/r8brain"
  includedirs { "../include/r8brain" }
  files {
   "../include/r8brain/r8bbase.cpp",
  }
  files {
   "../include/r8brain/CDSPBlockConvolver.h",
   "../include/r8brain/CDSPFIRFilter.h",
   "../include/r8brain/CDSPFracInterpolator.h",
   "../include/r8brain/CDSPProcessor.h",
   "../include/r8brain/CDSPRealFFT.h",
   "../include/r8brain/CDSPResampler.h",
   "../include/r8brain/CDSPSincFilterGen.h",
   "../include/r8brain/fft4g.h",
   "../include/r8brain/r8bbase.h",
   "../include/r8brain/r8bconf.h",
   "../include/r8brain/r8butil.h",
  }
  dofile "../build/premake4-defaults-LIB.lua"
  dofile "../build/premake4-defaults-static.lua"
 