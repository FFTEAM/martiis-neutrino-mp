/*
	Neutrino-GUI  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
                      2003 thegoodguy
	Copyright (C) 2007-2013 Stefan Seyfried

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef MARTII
#define _FILE_OFFSET_BITS 64
#include <png.h>
#endif
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <driver/framebuffer_ng.h>

#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <memory.h>
#include <math.h>

#include <linux/kd.h>

#ifdef USE_OPENGL
#include <glfb.h>
extern GLFramebuffer *glfb;
#endif

#include <gui/audiomute.h>
#include <gui/color.h>
#include <gui/pictureviewer.h>
#include <global.h>
#include <video.h>
#include <cs_api.h>
#ifdef HAVE_COOL_HARDWARE
#include <cnxtfb.h>
#endif
#if HAVE_TRIPLEDRAGON
#include <tdgfx/stb04gfx.h>
extern int gfxfd;
#endif

extern CPictureViewer * g_PicViewer;
#define ICON_CACHE_SIZE 1024*1024*2 // 2mb

#define BACKGROUNDIMAGEWIDTH 720

/* note that it is *not* enough to just change those values */
#define DEFAULT_XRES 1280
#define DEFAULT_YRES 720
#define DEFAULT_BPP  32

/*******************************************************************************/

void CFrameBuffer::waitForIdle(const char *)
{
	accel->waitForIdle();
}

/*******************************************************************************/

static uint8_t * virtual_fb = NULL;
inline unsigned int make16color(uint16_t r, uint16_t g, uint16_t b, uint16_t t,
                                  uint32_t  /*rl*/ = 0, uint32_t  /*ro*/ = 0,
                                  uint32_t  /*gl*/ = 0, uint32_t  /*go*/ = 0,
                                  uint32_t  /*bl*/ = 0, uint32_t  /*bo*/ = 0,
                                  uint32_t  /*tl*/ = 0, uint32_t  /*to*/ = 0)
{
        return ((t << 24) & 0xFF000000) | ((r << 8) & 0xFF0000) | ((g << 0) & 0xFF00) | (b >> 8 & 0xFF);
}

/* this table contains the x coordinates for a quarter circle (the bottom right quarter) with fixed
   radius of 540 px which is the half of the max HD graphics size of 1080 px. So with that table we
    ca draw boxes with round corners and als circles by just setting dx = dy = radius (max 540). */
static const int q_circle[541] = {
	540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540, 540,
	540, 540, 540, 540, 539, 539, 539, 539, 539, 539, 539, 539, 539, 539, 539, 539, 539, 539, 539, 539,
	539, 538, 538, 538, 538, 538, 538, 538, 538, 538, 538, 538, 538, 537, 537, 537, 537, 537, 537, 537,
	537, 537, 536, 536, 536, 536, 536, 536, 536, 536, 535, 535, 535, 535, 535, 535, 535, 535, 534, 534,
	534, 534, 534, 534, 533, 533, 533, 533, 533, 533, 532, 532, 532, 532, 532, 532, 531, 531, 531, 531,
	531, 531, 530, 530, 530, 530, 529, 529, 529, 529, 529, 529, 528, 528, 528, 528, 527, 527, 527, 527,
	527, 526, 526, 526, 526, 525, 525, 525, 525, 524, 524, 524, 524, 523, 523, 523, 523, 522, 522, 522,
	522, 521, 521, 521, 521, 520, 520, 520, 519, 519, 519, 518, 518, 518, 518, 517, 517, 517, 516, 516,
	516, 515, 515, 515, 515, 514, 514, 514, 513, 513, 513, 512, 512, 512, 511, 511, 511, 510, 510, 510,
	509, 509, 508, 508, 508, 507, 507, 507, 506, 506, 506, 505, 505, 504, 504, 504, 503, 503, 502, 502,
	502, 501, 501, 500, 500, 499, 499, 499, 498, 498, 498, 497, 497, 496, 496, 496, 495, 495, 494, 494,
	493, 493, 492, 492, 491, 491, 490, 490, 490, 489, 489, 488, 488, 487, 487, 486, 486, 485, 485, 484,
	484, 483, 483, 482, 482, 481, 481, 480, 480, 479, 479, 478, 478, 477, 477, 476, 476, 475, 475, 474,
	473, 473, 472, 472, 471, 471, 470, 470, 469, 468, 468, 467, 466, 466, 465, 465, 464, 464, 463, 462,
	462, 461, 460, 460, 459, 459, 458, 458, 457, 456, 455, 455, 454, 454, 453, 452, 452, 451, 450, 450,
	449, 449, 448, 447, 446, 446, 445, 445, 444, 443, 442, 441, 441, 440, 440, 439, 438, 437, 436, 436,
	435, 435, 434, 433, 432, 431, 431, 430, 429, 428, 427, 427, 426, 425, 425, 424, 423, 422, 421, 421,
	420, 419, 418, 417, 416, 416, 415, 414, 413, 412, 412, 411, 410, 409, 408, 407, 406, 405, 404, 403,
	403, 402, 401, 400, 399, 398, 397, 397, 395, 394, 393, 393, 392, 391, 390, 389, 388, 387, 386, 385,
	384, 383, 382, 381, 380, 379, 378, 377, 376, 375, 374, 373, 372, 371, 369, 368, 367, 367, 365, 364,
	363, 362, 361, 360, 358, 357, 356, 355, 354, 353, 352, 351, 350, 348, 347, 346, 345, 343, 342, 341,
	340, 339, 337, 336, 335, 334, 332, 331, 329, 328, 327, 326, 324, 323, 322, 321, 319, 317, 316, 315,
	314, 312, 310, 309, 308, 307, 305, 303, 302, 301, 299, 297, 296, 294, 293, 291, 289, 288, 287, 285,
	283, 281, 280, 278, 277, 275, 273, 271, 270, 268, 267, 265, 263, 261, 259, 258, 256, 254, 252, 250,
	248, 246, 244, 242, 240, 238, 236, 234, 232, 230, 228, 225, 223, 221, 219, 217, 215, 212, 210, 207,
	204, 202, 200, 197, 195, 192, 190, 187, 184, 181, 179, 176, 173, 170, 167, 164, 160, 157, 154, 150,
	147, 144, 140, 136, 132, 128, 124, 120, 115, 111, 105, 101,  95,  89,  83,  77,  69,  61,  52,  40,
	 23};

static inline bool calcCorners(int *ofs, int *ofl, int *ofr, const int& dy, const int& line, const int& radius,
				const bool& tl, const bool& tr, const bool& bl, const bool& br)
{
/* just a multiplicator for all math to reduce rounding errors */
#define MUL 32768
	int scl, _ofs = 0;
	bool ret = false;
	if (ofl != NULL) *ofl = 0;
	if (ofr != NULL) *ofr = 0;
	int scf = 540 * MUL / radius;
	/* one of the top corners */
	if (line < radius && (tl || tr)) {
		/* uper round corners */
		scl = scf * (radius - line) / MUL;
		if ((scf * (radius - line) % MUL) >= (MUL / 2)) /* round up */
			scl++;
		_ofs =  radius - (q_circle[scl] * MUL / scf);
		if (ofl != NULL && tl) *ofl = _ofs;
		if (ofr != NULL && tr) *ofr = _ofs;
	}
	/* one of the bottom corners */
	else if ((line >= dy - radius) && (bl || br)) {
		/* lower round corners */
		scl = scf * (radius - (dy - (line + 1))) / MUL;
		if ((scf * (radius - (dy - (line + 1))) % MUL) >= (MUL / 2)) /* round up */
			scl++;
		_ofs =  radius - (q_circle[scl] * MUL / scf);
		if (ofl != NULL && bl) *ofl = _ofs;
		if (ofr != NULL && br) *ofr = _ofs;
	}
	else
		ret = true;
	if (ofs != NULL) *ofs = _ofs;
	return ret;
}

static inline int limitRadius(const int& dx, const int& dy, const int& radius)
{
	int m = std::min(dx, dy);
	if (radius > m)
		return m;
	if (radius > 540)
		return 540;
	return radius;
}

CFrameBuffer::CFrameBuffer()
: active ( true )
{
	iconBasePath = "";
	available  = 0;
	cmap.start = 0;
	cmap.len = 256;
	cmap.red = red;
	cmap.green = green;
	cmap.blue  = blue;
	cmap.transp = trans;
	backgroundColor = 0;
	useBackgroundPaint = false;
	background = NULL;
	backupBackground = NULL;
	backgroundFilename = "";
	fd  = 0;
	tty = 0;
	bpp = 0;
	locked = false;
	m_transparent_default = CFrameBuffer::TM_BLACK; // TM_BLACK: Transparency when black content ('pseudo' transparency)
							// TM_NONE:  No 'pseudo' transparency
							// TM_INI:   Transparency depends on g_settings.infobar_alpha ???
	m_transparent         = m_transparent_default;
//FIXME: test
	memset(red, 0, 256*sizeof(__u16));
	memset(green, 0, 256*sizeof(__u16));
	memset(blue, 0, 256*sizeof(__u16));
	memset(trans, 0, 256*sizeof(__u16));
}

CFrameBuffer* CFrameBuffer::getInstance()
{
	static CFrameBuffer* frameBuffer = NULL;

	if(!frameBuffer) {
		frameBuffer = new CFrameBuffer();
		printf("[neutrino] frameBuffer Instance created\n");
	} else {
		//printf("[neutrino] frameBuffer Instace requested\n");
	}
	return frameBuffer;
}

#ifdef USE_NEVIS_GXA
void CFrameBuffer::setupGXA(void)
{
	accel->setupGXA();
}
#endif

void CFrameBuffer::init(const char * const fbDevice)
{
        int tr = 0xFF;
#ifdef USE_OPENGL
	(void)fbDevice;
	fd = -1;
	if (!glfb) {
		fprintf(stderr, "CFrameBuffer::init: GL Framebuffer is not set up? we are doomed...\n");
		goto nolfb;
	}
	screeninfo = glfb->getScreenInfo();
	stride = 4 * screeninfo.xres;
	available = glfb->getOSDBuffer()->size(); /* allocated in glfb constructor */
	lfb = reinterpret_cast<fb_pixel_t*>(glfb->getOSDBuffer()->data());
	memset(lfb, 0x7f, stride * screeninfo.yres);
#else
	fd = open(fbDevice, O_RDWR);
	if(!fd) fd = open(fbDevice, O_RDWR);

	if (fd<0) {
		perror(fbDevice);
		goto nolfb;
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &screeninfo)<0) {
		perror("FBIOGET_VSCREENINFO");
		goto nolfb;
	}

	memmove(&oldscreen, &screeninfo, sizeof(screeninfo));

	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix)<0) {
		perror("FBIOGET_FSCREENINFO");
		goto nolfb;
	}

	available=fix.smem_len;
	printf("%dk video mem\n", available/1024);
	lfb=(fb_pixel_t*)mmap(0, available, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);

	if (lfb == MAP_FAILED) {
		perror("mmap");
		goto nolfb;
	}

	memset(lfb, 0, available);
#endif /* USE_OPENGL */
	cache_size = 0;

        /* Windows Colors */
        paletteSetColor(0x1, 0x010101, tr);
        paletteSetColor(0x2, 0x800000, tr);
        paletteSetColor(0x3, 0x008000, tr);
        paletteSetColor(0x4, 0x808000, tr);
        paletteSetColor(0x5, 0x000080, tr);
        paletteSetColor(0x6, 0x800080, tr);
        paletteSetColor(0x7, 0x008080, tr);
        paletteSetColor(0x8, 0xA0A0A0, tr);
        paletteSetColor(0x9, 0x505050, tr);
        paletteSetColor(0xA, 0xFF0000, tr);
        paletteSetColor(0xB, 0x00FF00, tr);
        paletteSetColor(0xC, 0xFFFF00, tr);
        paletteSetColor(0xD, 0x0000FF, tr);
        paletteSetColor(0xE, 0xFF00FF, tr);
        paletteSetColor(0xF, 0x00FFFF, tr);
        paletteSetColor(0x10, 0xFFFFFF, tr);
        paletteSetColor(0x11, 0x000000, tr);
        paletteSetColor(COL_BACKGROUND, 0x000000, 0xffff);

        paletteSet();

        useBackground(false);
	m_transparent = m_transparent_default;
	accel = new CFbAccel(this);
	return;

nolfb:
	printf("framebuffer not available.\n");
	lfb=0;
}


CFrameBuffer::~CFrameBuffer()
{
	active = false; /* keep people/infoclocks from accessing */
	std::map<std::string, rawIcon>::iterator it;

	for(it = icon_cache.begin(); it != icon_cache.end(); ++it) {
		/* printf("FB: delete cached icon %s: %x\n", it->first.c_str(), (int) it->second.data); */
		cs_free_uncached(it->second.data);
	}
	icon_cache.clear();

	if (background) {
		delete[] background;
		background = NULL;
	}

	if (backupBackground) {
		delete[] backupBackground;
		backupBackground = NULL;
	}

	if (lfb)
		munmap(lfb, available);

	if (virtual_fb){
		delete[] virtual_fb;
		virtual_fb = NULL;
	}
	delete accel;
	close(fd);
}

int CFrameBuffer::getFileHandle() const
{
	return fd;
}

unsigned int CFrameBuffer::getStride() const
{
	return stride;
}

#ifdef MARTII
unsigned int CFrameBuffer::getScreenWidth(bool)
{
	return DEFAULT_XRES;
}

unsigned int CFrameBuffer::getScreenHeight(bool)
{
	return DEFAULT_YRES;
}

unsigned int CFrameBuffer::getScreenPercentRel(bool force_small)
{
	if (force_small || !g_settings.big_windows)
		return NON_BIG_WINDOWS;
	return 100;
}

unsigned int CFrameBuffer::getScreenWidthRel(bool force_small)
{
	int percent = getScreenPercentRel(force_small);
	// always reduce a possible detailline
	return (DEFAULT_XRES - 2*ConnectLineBox_Width) * percent / 100;
}

unsigned int CFrameBuffer::getScreenHeightRel(bool force_small)
{
	int percent = getScreenPercentRel(force_small);
	return DEFAULT_YRES * percent / 100;
}

unsigned int CFrameBuffer::getScreenX()
{
	return 0;
}

unsigned int CFrameBuffer::getScreenY()
{
	return 0;
}
#else
unsigned int CFrameBuffer::getScreenWidth(bool real)
{
	if(real)
		return xRes;
	else
		return g_settings.screen_EndX - g_settings.screen_StartX;
}

unsigned int CFrameBuffer::getScreenHeight(bool real)
{
	if(real)
		return yRes;
	else
		return g_settings.screen_EndY - g_settings.screen_StartY;
}


unsigned int CFrameBuffer::getScreenPercentRel(bool force_small)
{
	if (force_small || !g_settings.big_windows)
		return NON_BIG_WINDOWS;
	return 100;
}

unsigned int CFrameBuffer::getScreenWidthRel(bool force_small)
{
	int percent = getScreenPercentRel(force_small);
	// always reduce a possible detailline
	return (g_settings.screen_EndX - g_settings.screen_StartX - 2*ConnectLineBox_Width) * percent / 100;
}

unsigned int CFrameBuffer::getScreenHeightRel(bool force_small)
{
	int percent = getScreenPercentRel(force_small);
	return (g_settings.screen_EndY - g_settings.screen_StartY) * percent / 100;
}

unsigned int CFrameBuffer::getScreenX()
{
	return g_settings.screen_StartX;
}

unsigned int CFrameBuffer::getScreenY()
{
	return g_settings.screen_StartY;
}
#endif

#ifdef MARTII
fb_pixel_t * CFrameBuffer::getFrameBufferPointer(bool real)
#else
fb_pixel_t * CFrameBuffer::getFrameBufferPointer() const
#endif
{
#ifdef MARTII
	if (real)
		return lfb;
#endif
	if (active || (virtual_fb == NULL))
		return accel->lbb;
	else
		return (fb_pixel_t *)virtual_fb;
}

fb_pixel_t * CFrameBuffer::getBackBufferPointer() const
{
	return accel->backbuffer;
}

bool CFrameBuffer::getActive() const
{
	return (active || (virtual_fb != NULL));
}

void CFrameBuffer::setActive(bool enable)
{
	active = enable;
}

t_fb_var_screeninfo *CFrameBuffer::getScreenInfo()
{
	return &screeninfo;
}

int CFrameBuffer::setMode(unsigned int /*nxRes*/, unsigned int /*nyRes*/, unsigned int /*nbpp*/)
{
fprintf(stderr, "CFrameBuffer::setMode avail: %d active: %d\n", available, active);
	if (!available&&!active)
		return -1;

#if HAVE_AZBOX_HARDWARE
#ifndef FBIO_BLIT
#define FBIO_SET_MANUAL_BLIT _IOW('F', 0x21, __u8)
#define FBIO_BLIT 0x22
#endif
	// set manual blit
	unsigned char tmp = 1;
	if (ioctl(fd, FBIO_SET_MANUAL_BLIT, &tmp)<0)
		perror("FBIO_SET_MANUAL_BLIT");

	const unsigned int nxRes = DEFAULT_XRES;
	const unsigned int nyRes = DEFAULT_YRES;
	const unsigned int nbpp  = DEFAULT_BPP;
	screeninfo.xres_virtual = screeninfo.xres = nxRes;
	screeninfo.yres_virtual = (screeninfo.yres = nyRes) * 2;
	screeninfo.height = 0;
	screeninfo.width = 0;
	screeninfo.xoffset = screeninfo.yoffset = 0;
	screeninfo.bits_per_pixel = nbpp;

	screeninfo.transp.offset = 24;
	screeninfo.transp.length = 8;
	screeninfo.red.offset = 16;
	screeninfo.red.length = 8;
	screeninfo.green.offset = 8;
	screeninfo.green.length = 8;
	screeninfo.blue.offset = 0;
	screeninfo.blue.length = 8;

	if (ioctl(fd, FBIOPUT_VSCREENINFO, &screeninfo)<0) {
		// try single buffering
		screeninfo.yres_virtual = screeninfo.yres = nyRes;
		if (ioctl(fd, FBIOPUT_VSCREENINFO, &screeninfo) < 0)
		perror("FBIOPUT_VSCREENINFO");
		printf("FB: double buffering not available.\n");
	}
	else
		printf("FB: double buffering available!\n");

	ioctl(fd, FBIOGET_VSCREENINFO, &screeninfo);

	if ((screeninfo.xres!=nxRes) && (screeninfo.yres!=nyRes) && (screeninfo.bits_per_pixel!=nbpp))
	{
		printf("SetMode failed: wanted: %dx%dx%d, got %dx%dx%d\n",
		       nxRes, nyRes, nbpp,
		       screeninfo.xres, screeninfo.yres, screeninfo.bits_per_pixel);
	}
#endif
#if HAVE_SPARK_HARDWARE
	/* it's all fake... :-) */
	screeninfo.xres = screeninfo.xres_virtual = DEFAULT_XRES;
	screeninfo.yres = screeninfo.yres_virtual = DEFAULT_YRES;
	screeninfo.bits_per_pixel = DEFAULT_BPP;
	stride = screeninfo.xres * screeninfo.bits_per_pixel / 8;
#else
#ifndef USE_OPENGL
	fb_fix_screeninfo _fix;

	if (ioctl(fd, FBIOGET_FSCREENINFO, &_fix)<0) {
		perror("FBIOGET_FSCREENINFO");
		return -1;
	}
	stride = _fix.line_length;
#endif
#endif
	xRes = screeninfo.xres;
	yRes = screeninfo.yres;
	bpp  = screeninfo.bits_per_pixel;
	printf("FB: %dx%dx%d line length %d. %s nevis GXA accelerator.\n", xRes, yRes, bpp, stride,
#ifdef USE_NEVIS_GXA
		"Using"
#else
		"Not using"
#endif
	);
	accel->update(); /* just in case we need to update stuff */

	//memset(getFrameBufferPointer(), 0, stride * yRes);
	paintBackground();
#if HAVE_COOL_HARDWARE
        if (ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK) < 0) {
                printf("screen unblanking failed\n");
        }
#endif
	return 0;
}
#if 0 
//never used
void CFrameBuffer::setTransparency( int /*tr*/ )
{
}
#endif

#if !HAVE_TRIPLEDRAGON
void CFrameBuffer::setBlendMode(uint8_t mode)
{
	(void)mode;
#ifdef HAVE_COOL_HARDWARE
	if (ioctl(fd, FBIO_SETBLENDMODE, mode))
		printf("FBIO_SETBLENDMODE failed.\n");
#endif
}

void CFrameBuffer::setBlendLevel(int level)
{
	(void)level;
#ifdef HAVE_COOL_HARDWARE
	//printf("CFrameBuffer::setBlendLevel %d\n", level);
	unsigned char value = 0xFF;
	if((level >= 0) && (level <= 100))
		value = convertSetupAlpha2Alpha(level);

	if (ioctl(fd, FBIO_SETOPACITY, value))
		printf("FBIO_SETOPACITY failed.\n");
#if 1
       if(level == 100) // TODO: sucks.
               usleep(20000);
#endif
#endif
}
#else
/* TRIPLEDRAGON */
void CFrameBuffer::setBlendMode(uint8_t mode)
{
	Stb04GFXOsdControl g;
	ioctl(gfxfd, STB04GFX_OSD_GETCONTROL, &g);
	g.use_global_alpha = (mode == 2); /* 1 == pixel alpha, 2 == global alpha */
	ioctl(gfxfd, STB04GFX_OSD_SETCONTROL, &g);
}

void CFrameBuffer::setBlendLevel(int level)
{
	/* this is bypassing directfb, but faster and easier */
	Stb04GFXOsdControl g;
	ioctl(gfxfd, STB04GFX_OSD_GETCONTROL, &g);
	if (g.use_global_alpha == 0)
		return;

	if (level < 0 || level > 100)
		return;

	/* this is the same as convertSetupAlpha2Alpha(), but non-float */
	g.global_alpha = 255 - (255 * level / 100);
	ioctl(gfxfd, STB04GFX_OSD_SETCONTROL, &g);
	if (level == 100) // sucks
		usleep(20000);
}
#endif

#if 0 
//never used
void CFrameBuffer::setAlphaFade(int in, int num, int tr)
{
	for (int i=0; i<num; i++) {
		cmap.transp[in+i]=tr;
	}
}
#endif
void CFrameBuffer::paletteFade(int i, __u32 rgb1, __u32 rgb2, int level)
{
	__u16 *r = cmap.red+i;
	__u16 *g = cmap.green+i;
	__u16 *b = cmap.blue+i;
	*r= ((rgb2&0xFF0000)>>16)*level;
	*g= ((rgb2&0x00FF00)>>8 )*level;
	*b= ((rgb2&0x0000FF)    )*level;
	*r+=((rgb1&0xFF0000)>>16)*(255-level);
	*g+=((rgb1&0x00FF00)>>8 )*(255-level);
	*b+=((rgb1&0x0000FF)    )*(255-level);
}

void CFrameBuffer::paletteGenFade(int in, __u32 rgb1, __u32 rgb2, int num, int tr)
{
	for (int i=0; i<num; i++) {
		paletteFade(in+i, rgb1, rgb2, i*(255/(num-1)));
		cmap.transp[in+i]=tr;
		tr--; //FIXME
	}
}

void CFrameBuffer::paletteSetColor(int i, __u32 rgb, int tr)
{
	cmap.red[i]    =(rgb&0xFF0000)>>8;
	cmap.green[i]  =(rgb&0x00FF00)   ;
	cmap.blue[i]   =(rgb&0x0000FF)<<8;
	cmap.transp[i] = tr;
}

void CFrameBuffer::paletteSet(struct fb_cmap *map)
{
	if (!active)
		return;

	if(map == NULL)
		map = &cmap;

	if(bpp == 8) {
//printf("Set palette for %dbit\n", bpp);
		ioctl(fd, FBIOPUTCMAP, map);
	}
	uint32_t  rl, ro, gl, go, bl, bo, tl, to;
        rl = screeninfo.red.length;
        ro = screeninfo.red.offset;
        gl = screeninfo.green.length;
        go = screeninfo.green.offset;
        bl = screeninfo.blue.length;
        bo = screeninfo.blue.offset;
        tl = screeninfo.transp.length;
        to = screeninfo.transp.offset;
	for (int i = 0; i < 256; i++) {
                realcolor[i] = make16color(cmap.red[i], cmap.green[i], cmap.blue[i], cmap.transp[i],
                                           rl, ro, gl, go, bl, bo, tl, to);
	}
}

void CFrameBuffer::paintBoxRel(const int x, const int y, const int dx, const int dy, const fb_pixel_t col, int radius, int type)
{
	/* draw a filled rectangle (with additional round corners) */
	if (!getActive())
		return;

	bool corner_tl = !!(type & CORNER_TOP_LEFT);
	bool corner_tr = !!(type & CORNER_TOP_RIGHT);
	bool corner_bl = !!(type & CORNER_BOTTOM_LEFT);
	bool corner_br = !!(type & CORNER_BOTTOM_RIGHT);

	checkFbArea(x, y, dx, dy, true);

	if (!type || !radius)
	{
		accel->paintRect(x, y, dx, dy, col);
		checkFbArea(x, y, dx, dy, false);
		return;
	}

	/* limit the radius */
	radius = limitRadius(dx, dy, radius);
	if (radius < 1)		/* dx or dy = 0... */
		radius = 1;	/* avoid div by zero below */

	int line = 0;
	while (line < dy) {
		int ofl, ofr;
		if (calcCorners(NULL, &ofl, &ofr, dy, line, radius,
				corner_tl, corner_tr, corner_bl, corner_br))
		{
			int height = dy - ((corner_tl || corner_tr)?radius: 0 ) - ((corner_bl || corner_br) ? radius : 0);
			accel->paintRect(x, y + line, dx, height, col);
			line += height;
			continue;
		}
		if (dx - ofr - ofl < 1) {
			//printf("FB-NG::%s:%d x %d y %d dx %d dy %d l %d r %d\n", __func__, __LINE__, x,y,dx,dy, ofl, ofr);
			line++;
			continue;
		}
		accel->paintLine(x + ofl, y + line, x + dx - ofr, y + line, col);
		line++;
	}
	checkFbArea(x, y, dx, dy, false);
}

void CFrameBuffer::paintPixel(const int x, const int y, const fb_pixel_t col)
{
	if (!getActive())
		return;
	if (x > (int)xRes || y > (int)yRes || x < 0 || y < 0)
		return;

	accel->paintPixel(x, y, col);
}

void CFrameBuffer::paintLine(int xa, int ya, int xb, int yb, const fb_pixel_t col)
{
	if (!getActive())
		return;

	accel->paintLine(xa, ya, xb, yb, col);
}

void CFrameBuffer::paintVLine(int x, int ya, int yb, const fb_pixel_t col)
{
	paintLine(x, ya, x, yb, col);
}

void CFrameBuffer::paintVLineRel(int x, int y, int dy, const fb_pixel_t col)
{
	paintLine(x, y, x, y + dy, col);
}

void CFrameBuffer::paintHLine(int xa, int xb, int y, const fb_pixel_t col)
{
	paintLine(xa, y, xb, y, col);
}

void CFrameBuffer::paintHLineRel(int x, int dx, int y, const fb_pixel_t col)
{
	paintLine(x, y, x + dx, y, col);
}

void CFrameBuffer::setIconBasePath(const std::string & iconPath)
{
	iconBasePath = iconPath;
}

void CFrameBuffer::getIconSize(const char * const filename, int* width, int *height)
{
	*width = 0;
	*height = 0;

	if(filename == NULL)
		return;

	std::map<std::string, rawIcon>::iterator it;

	/* if code ask for size, lets cache it. assume we have enough ram for cache */
	/* FIXME offset seems never used in code, always default = 1 ? */

	it = icon_cache.find(filename);
	if(it == icon_cache.end()) {
		if(paintIcon(filename, 0, 0, 0, 1, false)) {
			it = icon_cache.find(filename);
		}
	}
	if(it != icon_cache.end()) {
		*width = it->second.width;
		*height = it->second.height;
	}
}

bool CFrameBuffer::paintIcon8(const std::string & filename, const int x, const int y, const unsigned char offset)
{
	if (!getActive())
		return false;

//printf("%s(file, %d, %d, %d)\n", __FUNCTION__, x, y, offset);

	struct rawHeader header;
	uint16_t         width, height;
	int              lfd;

	lfd = open((iconBasePath + filename).c_str(), O_RDONLY);

	if (lfd == -1) {
		printf("paintIcon8: error while loading icon: %s%s\n", iconBasePath.c_str(), filename.c_str());
		return false;
	}

	read(lfd, &header, sizeof(struct rawHeader));

	width  = (header.width_hi  << 8) | header.width_lo;
	height = (header.height_hi << 8) | header.height_lo;

	unsigned char pixbuf[768];

	uint8_t * d = ((uint8_t *)getFrameBufferPointer()) + x * sizeof(fb_pixel_t) + stride * y;
	fb_pixel_t * d2;
	for (int count=0; count<height; count ++ ) {
		read(lfd, &pixbuf[0], width );
		unsigned char *pixpos = &pixbuf[0];
		d2 = (fb_pixel_t *) d;
		for (int count2=0; count2<width; count2 ++ ) {
			unsigned char color = *pixpos;
			if (color != header.transp) {
//printf("icon8: col %d transp %d real %08X\n", color+offset, header.transp, realcolor[color+offset]);
				paintPixel(d2, color + offset);
			}
			d2++;
			pixpos++;
		}
		d += stride;
	}
	close(lfd);
	blit();
	return true;
}

/* paint icon at position x/y,
   if height h is given, center vertically between y and y+h
   offset is a color offset (probably only useful with palette) */
bool CFrameBuffer::paintIcon(const std::string & filename, const int x, const int y,
			     const int h, const unsigned char offset, bool paint, bool paintBg, const fb_pixel_t colBg)
{
	struct rawHeader header;
	int         width, height;
	int              lfd;
	fb_pixel_t * data;
	struct rawIcon tmpIcon;
	std::map<std::string, rawIcon>::iterator it;
	int dsize;

	if (!getActive())
		return false;

	int  yy = y;
	//printf("CFrameBuffer::paintIcon: load %s\n", filename.c_str());fflush(stdout);

	/* we cache and check original name */
	it = icon_cache.find(filename);
	if(it == icon_cache.end()) {
		std::string newname = iconBasePath + filename.c_str() + ".png";
		//printf("CFrameBuffer::paintIcon: check for %s\n", newname.c_str());fflush(stdout);

		data = g_PicViewer->getIcon(newname, &width, &height);

		if(data) {
			dsize = width*height*sizeof(fb_pixel_t);
			//printf("CFrameBuffer::paintIcon: %s found, data %x size %d x %d\n", newname.c_str(), data, width, height);fflush(stdout);
			if(cache_size+dsize < ICON_CACHE_SIZE) {
				cache_size += dsize;
				tmpIcon.width = width;
				tmpIcon.height = height;
				tmpIcon.data = data;
				icon_cache.insert(std::pair <std::string, rawIcon> (filename, tmpIcon));
				//printf("Cached %s, cache size %d\n", newname.c_str(), cache_size);
			}
			goto _display;
		}

		newname = iconBasePath + filename.c_str() + ".raw";

		lfd = open(newname.c_str(), O_RDONLY);

		if (lfd == -1) {
			//printf("paintIcon: error while loading icon: %s\n", newname.c_str());
			return false;
		}
		read(lfd, &header, sizeof(struct rawHeader));

		tmpIcon.width = width  = (header.width_hi  << 8) | header.width_lo;
		tmpIcon.height = height = (header.height_hi << 8) | header.height_lo;

		dsize = width*height*sizeof(fb_pixel_t);

		tmpIcon.data = (fb_pixel_t*) cs_malloc_uncached(dsize);
		data = tmpIcon.data;

		unsigned char pixbuf[768];
		for (int count = 0; count < height; count ++ ) {
			read(lfd, &pixbuf[0], width >> 1 );
			unsigned char *pixpos = &pixbuf[0];
			for (int count2 = 0; count2 < width >> 1; count2 ++ ) {
				unsigned char compressed = *pixpos;
				unsigned char pix1 = (compressed & 0xf0) >> 4;
				unsigned char pix2 = (compressed & 0x0f);
				if (pix1 != header.transp)
					*data++ = realcolor[pix1+offset];
				else
					*data++ = 0;
				if (pix2 != header.transp)
					*data++ = realcolor[pix2+offset];
				else
					*data++ = 0;
				pixpos++;
			}
		}
		close(lfd);

		data = tmpIcon.data;

		if(cache_size+dsize < ICON_CACHE_SIZE) {
			cache_size += dsize;
			icon_cache.insert(std::pair <std::string, rawIcon> (filename, tmpIcon));
			//printf("Cached %s, cache size %d\n", newname.c_str(), cache_size);
		}
	} else {
		data = it->second.data;
		width = it->second.width;
		height = it->second.height;
		//printf("paintIcon: already cached %s %d x %d\n", newname.c_str(), width, height);
	}
_display:
	if(!paint)
		return true;

	if (h != 0)
		yy += (h - height) / 2;

	checkFbArea(x, yy, width, height, true);
	if (paintBg)
		paintBoxRel(x, yy, width, height, colBg);
	blit2FB(data, width, height, x, yy);
	checkFbArea(x, yy, width, height, false);
	return true;
 
}

void CFrameBuffer::loadPal(const std::string & filename, const unsigned char offset, const unsigned char endidx)
{
	if (!getActive())
		return;

//printf("%s()\n", __FUNCTION__);

	struct rgbData rgbdata;
	int            lfd;

	lfd = open((iconBasePath + filename).c_str(), O_RDONLY);

	if (lfd == -1) {
		printf("error while loading palette: %s%s\n", iconBasePath.c_str(), filename.c_str());
		return;
	}

	int pos = 0;
	int readb = read(lfd, &rgbdata,  sizeof(rgbdata) );
	while(readb) {
		__u32 rgb = (rgbdata.r<<16) | (rgbdata.g<<8) | (rgbdata.b);
		int colpos = offset+pos;
		if( colpos>endidx)
			break;

		paletteSetColor(colpos, rgb, 0xFF);
		readb = read(lfd, &rgbdata,  sizeof(rgbdata) );
		pos++;
	}
	paletteSet(&cmap);
	close(lfd);
}


void CFrameBuffer::paintBoxFrame(const int sx, const int sy, const int dx, const int dy, const int px, const fb_pixel_t col, const int rad, int type)
{
	if (!getActive())
		return;

	int radius = rad;
	bool corner_tl = !!(type & CORNER_TOP_LEFT);
	bool corner_tr = !!(type & CORNER_TOP_RIGHT);
	bool corner_bl = !!(type & CORNER_BOTTOM_LEFT);
	bool corner_br = !!(type & CORNER_BOTTOM_RIGHT);

	int r_tl = 0, r_tr = 0, r_bl = 0, r_br = 0;
	if (type && radius) {
		int x_rad = radius - 1;
		if (corner_tl) r_tl = x_rad;
		if (corner_tr) r_tr = x_rad;
		if (corner_bl) r_bl = x_rad;
		if (corner_br) r_br = x_rad;
	}
	paintBoxRel(sx + r_tl,    sy          , dx - r_tl - r_tr, px,               col); // top horizontal
	paintBoxRel(sx + r_bl,    sy + dy - px, dx - r_bl - r_br, px,               col); // bottom horizontal
	paintBoxRel(sx          , sy + r_tl,    px,               dy - r_tl - r_bl, col); // left vertical
	paintBoxRel(sx + dx - px, sy + r_tr,    px,               dy - r_tr - r_br, col); // right vertical

	if (!radius || !type)
		return;

	radius = limitRadius(dx, dy, radius);
	if (radius < 1)		/* dx or dy = 0... */
		radius = 1;	/* avoid div by zero below */
	int line = 0;
	while (line < dy) {
		int ofs = 0, ofs_i = 0;
		// inner box
		if ((line >= px) && (line < (dy - px)))
			calcCorners(&ofs_i, NULL, NULL, dy - 2 * px, line - px, radius - px,
					corner_tl, corner_tr, corner_bl, corner_br);
		// outer box
		calcCorners(&ofs, NULL, NULL, dy, line, radius, corner_tl, corner_tr, corner_bl, corner_br);

		int _y     = sy + line;
		if (line < px || line >= (dy - px)) {
			// left
			if ((corner_tl && line < radius) || (corner_bl && line >= dy - radius))
				accel->paintLine(sx + ofs, _y, sx + ofs + radius, _y, col);
			// right
			if ((corner_tr && line < radius) || (corner_br && line >= dy - radius))
				accel->paintLine(sx + dx - radius, _y, sx + dx - ofs, _y, col);
		}
		else if (line < (dy - px)) {
			int _dx = (ofs_i - ofs) + px;
			// left
			if ((corner_tl && line < radius) || (corner_bl && line >= dy - radius))
				accel->paintLine(sx + ofs, _y, sx + ofs + _dx, _y, col);
			// right
			if ((corner_tr && line < radius) || (corner_br && line >= dy - radius))
				accel->paintLine(sx + dx - ofs_i - px, _y, sx + dx - ofs_i - px + _dx, _y, col);
		}
		if (line == radius && dy > 2 * radius)
			// line outside the rounded corners
			line = dy - radius;
		else
			line++;
	}
}

void CFrameBuffer::useBackground(bool ub)
{
	useBackgroundPaint = ub;
	if(!useBackgroundPaint) {
		delete[] background;
		background=0;
	}
}

bool CFrameBuffer::getuseBackground(void)
{
	return useBackgroundPaint;
}

void CFrameBuffer::saveBackgroundImage(void)
{
	if (backupBackground != NULL){
		delete[] backupBackground;
		backupBackground = NULL;
	}
	backupBackground = background;
	//useBackground(false); // <- necessary since no background is available
	useBackgroundPaint = false;
	background = NULL;
}

void CFrameBuffer::restoreBackgroundImage(void)
{
	fb_pixel_t * tmp = background;

	if (backupBackground != NULL)
	{
		background = backupBackground;
		backupBackground = NULL;
	}
	else
		useBackground(false); // <- necessary since no background is available

	if (tmp != NULL){
		delete[] tmp;
		tmp = NULL;
	}
}

void CFrameBuffer::paintBackgroundBoxRel(int x, int y, int dx, int dy)
{
	if (!getActive())
		return;

	if(!useBackgroundPaint)
	{
		/* paintBoxRel does its own checkFbArea() */
		paintBoxRel(x, y, dx, dy, backgroundColor);
	}
	else
	{
		checkFbArea(x, y, dx, dy, true);
		uint8_t * fbpos = ((uint8_t *)getFrameBufferPointer()) + x * sizeof(fb_pixel_t) + stride * y;
		fb_pixel_t * bkpos = background + x + BACKGROUNDIMAGEWIDTH * y;
		for(int count = 0;count < dy; count++)
		{
			memmove(fbpos, bkpos, dx * sizeof(fb_pixel_t));
			fbpos += stride;
			bkpos += BACKGROUNDIMAGEWIDTH;
		}
		checkFbArea(x, y, dx, dy, false);
	}
}

void CFrameBuffer::paintBackground()
{
	if (!getActive())
		return;

	if (useBackgroundPaint && (background != NULL))
	{
		checkFbArea(0, 0, xRes, yRes, true);
		/* this does not really work anyway... */
		for (int i = 0; i < 576; i++)
			memmove(((uint8_t *)getFrameBufferPointer()) + i * stride, (background + i * BACKGROUNDIMAGEWIDTH), BACKGROUNDIMAGEWIDTH * sizeof(fb_pixel_t));
		checkFbArea(0, 0, xRes, yRes, false);
	}
	else
	{
		paintBoxRel(0, 0, xRes, yRes, backgroundColor);
	}
	blit();
}

void CFrameBuffer::SaveScreen(int x, int y, int dx, int dy, fb_pixel_t * const memp)
{
	if (!getActive() || memp == NULL)
		return;

	checkFbArea(x, y, dx, dy, true);
	uint8_t * pos = ((uint8_t *)getFrameBufferPointer()) + x * sizeof(fb_pixel_t) + stride * y;
	fb_pixel_t * bkpos = memp;
	for (int count = 0; count < dy; count++) {
		fb_pixel_t * dest = (fb_pixel_t *)pos;
		for (int i = 0; i < dx; i++)
			*(bkpos++) = *(dest++);
		pos += stride;
	}
#if 0
	/* todo: check what the problem with this is, it should be better -- probably caching issue */
	uint8_t * fbpos = ((uint8_t *)getFrameBufferPointer()) + x * sizeof(fb_pixel_t) + stride * y;
	fb_pixel_t * bkpos = memp;
	for (int count = 0; count < dy; count++)
	{
		memmove(bkpos, fbpos, dx * sizeof(fb_pixel_t));
		fbpos += stride;
		bkpos += dx;
	}
#endif
	checkFbArea(x, y, dx, dy, false);
}

void CFrameBuffer::RestoreScreen(int x, int y, int dx, int dy, fb_pixel_t * const memp)
{
	if (!getActive() || memp == NULL)
		return;

	checkFbArea(x, y, dx, dy, true);
	uint8_t * fbpos = ((uint8_t *)getFrameBufferPointer()) + x * sizeof(fb_pixel_t) + stride * y;
	fb_pixel_t * bkpos = memp;
	for (int count = 0; count < dy; count++)
	{
		memmove(fbpos, bkpos, dx * sizeof(fb_pixel_t));
		fbpos += stride;
		bkpos += dx;
	}
	blit();
	checkFbArea(x, y, dx, dy, false);
}

void CFrameBuffer::Clear()
{
	paintBackground();
	//memset(getFrameBufferPointer(), 0, stride * yRes);
}

bool CFrameBuffer::Lock()
{
	if(locked)
		return false;
	locked = true;
	return true;
}

void CFrameBuffer::Unlock()
{
	locked = false;
}

void * CFrameBuffer::int_convertRGB2FB(unsigned char *rgbbuff, unsigned long x, unsigned long y, int transp, bool alpha)
{
	unsigned long i;
	unsigned int *fbbuff;
	unsigned long count = x * y;

	fbbuff = (unsigned int *) cs_malloc_uncached(count * sizeof(unsigned int));
	if(fbbuff == NULL) {
		printf("convertRGB2FB%s: Error: cs_malloc_uncached\n", ((alpha) ? " (Alpha)" : ""));
		return NULL;
	}

	if (alpha) {
		for(i = 0; i < count ; i++)
			fbbuff[i] = ((rgbbuff[i*4+3] << 24) & 0xFF000000) | 
			            ((rgbbuff[i*4]   << 16) & 0x00FF0000) | 
		        	    ((rgbbuff[i*4+1] <<  8) & 0x0000FF00) | 
			            ((rgbbuff[i*4+2])       & 0x000000FF);
	} else {
		switch (m_transparent) {
			case CFrameBuffer::TM_BLACK:
				for(i = 0; i < count ; i++) {
					transp = 0;
					if(rgbbuff[i*3] || rgbbuff[i*3+1] || rgbbuff[i*3+2])
						transp = 0xFF;
					fbbuff[i] = (transp << 24) | ((rgbbuff[i*3] << 16) & 0xFF0000) | ((rgbbuff[i*3+1] << 8) & 0xFF00) | (rgbbuff[i*3+2] & 0xFF);
				}
				break;
			case CFrameBuffer::TM_INI:
				for(i = 0; i < count ; i++)
					fbbuff[i] = (transp << 24) | ((rgbbuff[i*3] << 16) & 0xFF0000) | ((rgbbuff[i*3+1] << 8) & 0xFF00) | (rgbbuff[i*3+2] & 0xFF);
				break;
			case CFrameBuffer::TM_NONE:
			default:
				for(i = 0; i < count ; i++)
					fbbuff[i] = 0xFF000000 | ((rgbbuff[i*3] << 16) & 0xFF0000) | ((rgbbuff[i*3+1] << 8) & 0xFF00) | (rgbbuff[i*3+2] & 0xFF);
				break;
		}
	}
	return (void *) fbbuff;
}

void * CFrameBuffer::convertRGB2FB(unsigned char *rgbbuff, unsigned long x, unsigned long y, int transp)
{
	return int_convertRGB2FB(rgbbuff, x, y, transp, false);
}

void * CFrameBuffer::convertRGBA2FB(unsigned char *rgbbuff, unsigned long x, unsigned long y)
{
	return int_convertRGB2FB(rgbbuff, x, y, 0, true);
}

void CFrameBuffer::blit2FB(void *fbbuff, uint32_t width, uint32_t height, uint32_t xoff, uint32_t yoff, uint32_t xp, uint32_t yp, bool transp)
{
	accel->blit2FB(fbbuff, width, height, xoff, yoff, xp, yp, transp);
}

void CFrameBuffer::displayRGB(unsigned char *rgbbuff, int x_size, int y_size, int x_pan, int y_pan, int x_offs, int y_offs, bool clearfb, int transp)
{
        void *fbbuff = NULL;

        if(rgbbuff == NULL)
                return;

        /* correct panning */
        if(x_pan > x_size - (int)xRes) x_pan = 0;
        if(y_pan > y_size - (int)yRes) y_pan = 0;

        /* correct offset */
        if(x_offs + x_size > (int)xRes) x_offs = 0;
        if(y_offs + y_size > (int)yRes) y_offs = 0;

        /* blit buffer 2 fb */
        fbbuff = convertRGB2FB(rgbbuff, x_size, y_size, transp);
        if(fbbuff==NULL)
                return;

        /* ClearFB if image is smaller */
        /* if(x_size < (int)xRes || y_size < (int)yRes) */
        if(clearfb)
                CFrameBuffer::getInstance()->Clear();

        blit2FB(fbbuff, x_size, y_size, x_offs, y_offs, x_pan, y_pan);
        cs_free_uncached(fbbuff);
}

void CFrameBuffer::paintMuteIcon(bool paint, int ax, int ay, int dx, int dy, bool paintFrame)
{
	if(paint) {
		if (paintFrame) {
			paintBackgroundBoxRel(ax, ay, dx, dy);
			paintBoxRel(ax, ay, dx, dy, COL_MENUCONTENT_PLUS_0, RADIUS_SMALL);
		}
		int icon_dx=0, icon_dy=0;
		getIconSize(NEUTRINO_ICON_BUTTON_MUTE, &icon_dx, &icon_dy);
		paintIcon(NEUTRINO_ICON_BUTTON_MUTE, ax+((dx-icon_dx)/2), ay+((dy-icon_dy)/2));
	}
	else
		paintBackgroundBoxRel(ax, ay, dx, dy);
	blit();
}
#ifdef MARTII
CFrameBuffer::Mode3D CFrameBuffer::get3DMode()
{
	return mode3D;
}

void CFrameBuffer::set3DMode(Mode3D m)
{
	if (mode3D != m) {
		accel->ClearFB();
		mode3D = m;
		accel->borderColorOld = 0x01010101;
		blit();
	}
}

bool CFrameBuffer::OSDShot(const std::string &name)
{
	struct timeval ts, te;
	gettimeofday(&ts, NULL);

	size_t l = name.find_last_of(".");
	if(l == std::string::npos)
		return false;
	if (name.substr(l) != ".png")
		return false;
	FILE *out = fopen(name.c_str(), "w");
	if (!out)
		return false;

	unsigned int xres = DEFAULT_XRES;
	unsigned int yres = DEFAULT_YRES;
	fb_pixel_t *b = (fb_pixel_t *) accel->lbb;

	if (!g_settings.screenshot_backbuffer) {
		xres = accel->s.xres;
		yres = accel->s.yres;
		b = (fb_pixel_t *) lfb;
	}

	png_bytep row_pointers[yres];
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
		(png_voidp) NULL, (png_error_ptr) NULL, (png_error_ptr) NULL);
	png_infop info_ptr = png_create_info_struct(png_ptr);

	png_init_io(png_ptr, out);

	for (unsigned int y = 0; y < yres; y++)
		row_pointers[y] = (png_bytep) (b + y * xres);

	png_set_compression_level(png_ptr, g_settings.screenshot_png_compression);
	png_set_bgr(png_ptr);
	png_set_filter(png_ptr, 0, PNG_FILTER_NONE);
	png_set_IHDR(png_ptr, info_ptr, xres, yres, 8, PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
	png_write_info(png_ptr, info_ptr);
	png_write_image(png_ptr, row_pointers);
	png_write_end(png_ptr, NULL);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	fclose(out);

	gettimeofday(&te, NULL);
	fprintf(stderr, "%s took %lld us\n", __func__, (te.tv_sec * 1000000LL + te.tv_usec) - (ts.tv_sec * 1000000LL + ts.tv_usec));
	return true;
}

void CFrameBuffer::blitArea(int src_width, int src_height, int fb_x, int fb_y, int width, int height)
{
	accel->blitArea(src_width, src_height, fb_x, fb_y, width, height);
}

void CFrameBuffer::resChange(void)
{
	accel->resChange();
}

void CFrameBuffer::setBorder(int sx, int sy, int ex, int ey)
{
	accel->setBorder(sx, sy, ex, ey);
}

void CFrameBuffer::getBorder(int &sx, int &sy, int &ex, int &ey)
{
	accel->getBorder(sx, sy, ex, ey);
}

void CFrameBuffer::setBorderColor(fb_pixel_t col)
{
	accel->setBorderColor(col);
}

fb_pixel_t CFrameBuffer::getBorderColor(void)
{
	return accel->getBorderColor();
}

void CFrameBuffer::ClearFB(void)
{
	accel->ClearFB();
}
#endif

void CFrameBuffer::setFbArea(int element, int _x, int _y, int _dx, int _dy)
{
	if (_x == 0 && _y == 0 && _dx == 0 && _dy == 0) {
		// delete area
		for (fbarea_iterator_t it = v_fbarea.begin(); it != v_fbarea.end(); ++it) {
			if (it->element == element) {
				v_fbarea.erase(it);
				break;
			}
		}
		if (v_fbarea.empty()) {
			fbAreaActiv = false;
		}
	}
	else {
		// change area
		bool found = false;
		for (unsigned int i = 0; i < v_fbarea.size(); i++) {
			if (v_fbarea[i].element == element) {
				v_fbarea[i].x = _x;
				v_fbarea[i].y = _y;
				v_fbarea[i].dx = _dx;
				v_fbarea[i].dy = _dy;
				found = true;
				break;
			}
		}
		// set new area
		if (!found) {
			fb_area_t area;
			area.x = _x;
			area.y = _y;
			area.dx = _dx;
			area.dy = _dy;
			area.element = element;
			v_fbarea.push_back(area);
		}
		fbAreaActiv = true;
	}
}

int CFrameBuffer::checkFbAreaElement(int _x, int _y, int _dx, int _dy, fb_area_t *area)
{
	if (fb_no_check)
		return FB_PAINTAREA_MATCH_NO;

	if (_y > area->y + area->dy)
		return FB_PAINTAREA_MATCH_NO;
	if (_x + _dx < area->x)
		return FB_PAINTAREA_MATCH_NO;
	if (_x > area->x + area->dx)
		return FB_PAINTAREA_MATCH_NO;
	if (_y + _dy < area->y)
		return FB_PAINTAREA_MATCH_NO;
	return FB_PAINTAREA_MATCH_OK;
}

bool CFrameBuffer::_checkFbArea(int _x, int _y, int _dx, int _dy, bool prev)
{
	if (v_fbarea.empty())
		return true;

	for (unsigned int i = 0; i < v_fbarea.size(); i++) {
		int ret = checkFbAreaElement(_x, _y, _dx, _dy, &v_fbarea[i]);
		if (ret == FB_PAINTAREA_MATCH_OK) {
			switch (v_fbarea[i].element) {
				case FB_PAINTAREA_MUTEICON1:
					if (!do_paint_mute_icon)
						break;
					fb_no_check = true;
					if (prev)
						CAudioMute::getInstance()->hide(true);
					else
						CAudioMute::getInstance()->paint();
					fb_no_check = false;
					break;
				default:
					break;
			}
		}
	}

	return true;
}
