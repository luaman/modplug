
  configuration "x64"
   targetsuffix "64"
  
  configuration "Debug"
   targetdir "bin/Debug"
  
  configuration "Normal"
   targetdir "bin/Normal"
  
  configuration "Release"
   targetdir "bin/Release"
  
  configuration "*"
   kind "StaticLib"
   
  configuration "Debug"
   defines { "DEBUG" }
   defines { "WIN32", "_CRT_SECURE_NO_WARNINGS", "_CRT_NONSTDC_NO_DEPRECATE", "_CRT_SECURE_NO_DEPRECATE", "_CRT_NONSTDC_NO_WARNINGS" }
   flags { "Symbols" }
  
  configuration "Normal"
   defines { "NDEBUG" }
   defines { "WIN32", "_CRT_SECURE_NO_WARNINGS", "_CRT_NONSTDC_NO_DEPRECATE", "_CRT_SECURE_NO_DEPRECATE", "_CRT_NONSTDC_NO_WARNINGS" }
   flags { "Symbols", "Optimize", "FloatFast", "StaticRuntime" }
   buildoptions { "/GL- /MP" }

  configuration "Release"
   defines { "NDEBUG" }
   defines { "WIN32", "_CRT_SECURE_NO_WARNINGS", "_CRT_NONSTDC_NO_DEPRECATE", "_CRT_SECURE_NO_DEPRECATE", "_CRT_NONSTDC_NO_WARNINGS" }
   flags { "Symbols", "Optimize", "FloatFast", "StaticRuntime" }
   buildoptions { "/MP" }
