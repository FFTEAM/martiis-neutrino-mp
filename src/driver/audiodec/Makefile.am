AM_CXXFLAGS = -fno-rtti -fno-exceptions

AM_CPPFLAGS = \
	-I$(top_builddir) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/driver/audiodec \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/src/zapit/include \
	-I$(top_srcdir)/lib/libconfigfile \
	@FREETYPE_CFLAGS@ \
	@HWLIB_CFLAGS@

noinst_LIBRARIES = libneutrino_driver_audiodec.a

libneutrino_driver_audiodec_a_SOURCES = \
	basedec.cpp \
	ffmpegdec.cpp

