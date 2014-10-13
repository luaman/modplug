  
 project "portaudio"
  uuid "189B867F-FF4B-45A1-B741-A97492EE69AF"
  language "C"
  location "../build/gen"
  objdir "../build/obj/portaudio"
  includedirs { "../include/portaudio/include", "../include/portaudio/src/common", "../include/portaudio/src/os/win" }
  defines {
   "PAWIN_USE_WDMKS_DEVICE_INFO",
   "PA_WDMKS_NO_KSGUID_LIB",
   "PA_USE_ASIO=0",
   "PA_USE_DS=0",
   "PA_USE_WMME=1",
   "PA_USE_WASAPI=1",
   "PA_USE_WDMKS=1",
  }
  files {
   "../include/portaudio/src/common/pa_allocation.c",
   "../include/portaudio/src/common/pa_allocation.h",
   "../include/portaudio/src/common/pa_converters.c",
   "../include/portaudio/src/common/pa_converters.h",
   "../include/portaudio/src/common/pa_cpuload.c",
   "../include/portaudio/src/common/pa_cpuload.h",
   "../include/portaudio/src/common/pa_debugprint.c",
   "../include/portaudio/src/common/pa_debugprint.h",
   "../include/portaudio/src/common/pa_dither.c",
   "../include/portaudio/src/common/pa_dither.h",
   "../include/portaudio/src/common/pa_endianness.h",
   "../include/portaudio/src/common/pa_front.c",
   "../include/portaudio/src/common/pa_hostapi.h",
   "../include/portaudio/src/common/pa_memorybarrier.h",
   "../include/portaudio/src/common/pa_process.c",
   "../include/portaudio/src/common/pa_process.h",
   "../include/portaudio/src/common/pa_ringbuffer.c",
   "../include/portaudio/src/common/pa_ringbuffer.h",
   "../include/portaudio/src/common/pa_stream.c",
   "../include/portaudio/src/common/pa_stream.h",
   "../include/portaudio/src/common/pa_trace.c",
   "../include/portaudio/src/common/pa_trace.h",
   "../include/portaudio/src/common/pa_types.h",
   "../include/portaudio/src/common/pa_util.h",
   "../include/portaudio/src/hostapi/skeleton/pa_hostapi_skeleton.c",
   "../include/portaudio/src/hostapi/wasapi/pa_win_wasapi.c",
   "../include/portaudio/src/hostapi/wdmks/pa_win_wdmks.c",
   "../include/portaudio/src/hostapi/wmme/pa_win_wmme.c",
   "../include/portaudio/src/os/win/pa_win_coinitialize.c",
   "../include/portaudio/src/os/win/pa_win_coinitialize.h",
   "../include/portaudio/src/os/win/pa_win_hostapis.c",
   "../include/portaudio/src/os/win/pa_win_util.c",
   "../include/portaudio/src/os/win/pa_win_waveformat.c",
   "../include/portaudio/src/os/win/pa_win_wdmks_utils.c",
   "../include/portaudio/src/os/win/pa_win_wdmks_utils.h",
   "../include/portaudio/src/os/win/pa_x86_plain_converters.c",
   "../include/portaudio/src/os/win/pa_x86_plain_converters.h",
  }
  files {
   "../include/portaudio/include/pa_asio.h",
   "../include/portaudio/include/pa_jack.h",
   "../include/portaudio/include/pa_linux_alsa.h",
   "../include/portaudio/include/pa_mac_core.h",
   "../include/portaudio/include/pa_win_ds.h",
   "../include/portaudio/include/pa_win_wasapi.h",
   "../include/portaudio/include/pa_win_waveformat.h",
   "../include/portaudio/include/pa_win_wdmks.h",
   "../include/portaudio/include/pa_win_wmme.h",
   "../include/portaudio/include/portaudio.h",
  }
  buildoptions { "/wd4018", "/wd4267" }
  configuration "Debug*"
   defines { "PA_ENABLE_DEBUG_OUTPUT" }
  dofile "../build/premake4-defaults-LIB.lua"
  dofile "../build/premake4-defaults-static.lua"
