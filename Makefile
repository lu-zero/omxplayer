CXXFLAGS += -std=c++0x -Wall
CXXFLAGS += -D__STDC_CONSTANT_MACROS \
	    -D__STDC_LIMIT_MACROS \
            -DTARGET_POSIX -DTARGET_LINUX \
	    -fPIC -DPIC -D_REENTRANT \
	    -D_LARGEFILE64_SOURCE \
	    -D_FILE_OFFSET_BITS=64 \
	    -DHAVE_CMAKE_CONFIG \
	    -D__VIDEOCORE4__ \
	    -U_FORTIFY_SOURCE \
	    -DHAVE_OMXLIB \
	    -DUSE_EXTERNAL_FFMPEG \
	    -DHAVE_LIBAVCODEC_AVCODEC_H \
	    -DHAVE_LIBAVUTIL_OPT_H \
	    -DHAVE_LIBAVUTIL_MEM_H \
	    -DHAVE_LIBAVUTIL_AVUTIL_H \
	    -DHAVE_LIBAVFORMAT_AVFORMAT_H \
	    -DHAVE_LIBAVFILTER_AVFILTER_H \
	    -DHAVE_LIBAVRESAMPLE_AVRESAMPLE_H \
	    -DOMX \
	    -DOMX_SKIP64BIT \
	    -DUSE_EXTERNAL_OMX \
	    -DTARGET_RASPBERRY_PI \
	    -DUSE_EXTERNAL_LIBBCM_HOST

LDFLAGS += -L./ \
	   -lc \
	   -L/opt/vc/lib \
	   -lWFC -lGLESv2 -lEGL \
	   -lbcm_host -lopenmaxil \
	   -lfreetype -lz \
	   -Lthird_party/lib

LIBS += -lvchiq_arm -lvcos -lrt -lpthread
LIBS += -lavutil -lavcodec -lavformat -lswscale -lavresample
LIBS += -lpcre


INCLUDES+=-I./ -Ilinux -I/usr/armv6j-hardfloat-linux-gnueabi/opt/vc/include/ \
	-I/usr/armv6j-hardfloat-linux-gnueabi/opt/vc/include/interface/vcos/pthreads \
	-I/usr/armv6j-hardfloat-linux-gnueabi/opt/vc/include/interface/vmcs_host/linux

DIST ?= omxplayer-dist

SRC = linux/XMemUtils.cpp \
      utils/log.cpp \
      DynamicDll.cpp \
      utils/PCMRemap.cpp \
      utils/RegExp.cpp \
      OMXSubtitleTagSami.cpp \
      OMXOverlayCodecText.cpp \
      BitstreamConverter.cpp \
      linux/RBP.cpp \
      OMXThread.cpp \
      OMXReader.cpp \
      OMXStreamInfo.cpp \
      OMXAudioCodecOMX.cpp \
      OMXCore.cpp \
      OMXVideo.cpp \
      OMXAudio.cpp \
      OMXClock.cpp \
      File.cpp \
      OMXPlayerVideo.cpp \
      OMXPlayerAudio.cpp \
      OMXPlayerSubtitles.cpp \
      SubtitleRenderer.cpp \
      Unicode.cpp \
      Srt.cpp \
      KeyConfig.cpp \
      OMXControl.cpp \
      Keyboard.cpp \
      omxplayer.cpp \

OBJS += $(filter %.o, $(SRC:.cpp=.o))

all: omxplayer.bin

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

version:
	bash gen_version.sh > version.h

omxplayer.bin: version $(OBJS)
	$(CXX) $(LDFLAGS) -o omxplayer.bin $(OBJS) $(LIBS)

clean:
	@rm -f $(OBJS)
	@rm -f $(DIST)
	@rm -f omxplayer.bin

libav:
	make -f Makefile.libav
	make -f Makefile.libav install

dist: omxplayer.bin
	mkdir -p $(DIST)/usr/lib/omxplayer
	mkdir -p $(DIST)/usr/bin
	mkdir -p $(DIST)/usr/share/doc
	cp omxplayer omxplayer.bin $(DIST)/usr/bin
	cp COPYING $(DIST)/usr/share/doc/
	cp README.md $(DIST)/usr/share/doc/README
	cp -a third_party/lib/*.so* $(DIST)/usr/lib/omxplayer/
	tar -czf omxplayer-dist.tar.gz $(DIST)
