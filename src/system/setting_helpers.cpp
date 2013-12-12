/*
	Neutrino-GUI  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/

	Kommentar:

	Diese GUI wurde von Grund auf neu programmiert und sollte nun vom
	Aufbau und auch den Ausbaumoeglichkeiten gut aussehen. Neutrino basiert
	auf der Client-Server Idee, diese GUI ist also von der direkten DBox-
	Steuerung getrennt. Diese wird dann von Daemons uebernommen.


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
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <system/setting_helpers.h>
#include "configure_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <libnet.h>
#include <linux/if.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include <config.h>

#include <global.h>
#include <neutrino.h>
#include <gui/widget/stringinput.h>
#include <gui/infoclock.h>
#include <gui/infoviewer.h>
#include <gui/movieplayer.h>
#include <driver/volume.h>
#include <driver/display.h>
#include <system/helpers.h>
// obsolete #include <gui/streaminfo.h>

#include <gui/widget/messagebox.h>
#include <gui/widget/hintbox.h>

#include <gui/plugins.h>
#include <daemonc/remotecontrol.h>
#include <xmlinterface.h>
#include <audio.h>
#include <video.h>
#include <dmx.h>
#include <cs_api.h>
#include <pwrmngr.h>
#include <libdvbsub/dvbsub.h>
#include <libtuxtxt/teletext.h>
#include <zapit/satconfig.h>
#include <zapit/zapit.h>

extern CPlugins       * g_PluginList;    /* neutrino.cpp */
extern CRemoteControl * g_RemoteControl; /* neutrino.cpp */
extern cVideo *videoDecoder;
extern cAudio *audioDecoder;

extern cDemux *videoDemux;
extern cDemux *audioDemux;
extern cDemux *pcrDemux;

extern CHintBox *reloadhintBox;

extern "C" int pinghost( const char *hostname );

COnOffNotifier::COnOffNotifier(int OffValue)
{
	offValue = OffValue;
}

bool COnOffNotifier::changeNotify(const neutrino_locale_t, void *Data)
{
	bool active = (*(int*)(Data) != offValue);

	for (std::vector<CMenuItem*>::iterator it = toDisable.begin(); it != toDisable.end(); it++)
		(*it)->setActive(active);

	return false;
}

void COnOffNotifier::addItem(CMenuItem* menuItem)
{
	toDisable.push_back(menuItem);
}

bool CSectionsdConfigNotifier::changeNotify(const neutrino_locale_t, void *)
{
        CNeutrinoApp::getInstance()->SendSectionsdConfig();
        return false;
}

#if 0
bool CTouchFileNotifier::changeNotify(const neutrino_locale_t, void * data)
{
	if ((*(int *)data) != 0)
	{
		FILE * fd = fopen(filename, "w");
		if (fd)
			fclose(fd);
		else
			return false;
	}
	else
		remove(filename);
	return true;
}
#endif

void CColorSetupNotifier::setPalette()
{
	CFrameBuffer *frameBuffer = CFrameBuffer::getInstance();
	//setting colors-..
	frameBuffer->paletteGenFade(COL_MENUHEAD,
	                              convertSetupColor2RGB(g_settings.menu_Head_red, g_settings.menu_Head_green, g_settings.menu_Head_blue),
	                              convertSetupColor2RGB(g_settings.menu_Head_Text_red, g_settings.menu_Head_Text_green, g_settings.menu_Head_Text_blue),
	                              8, convertSetupAlpha2Alpha( g_settings.menu_Head_alpha ) );

	frameBuffer->paletteGenFade(COL_MENUCONTENT,
	                              convertSetupColor2RGB(g_settings.menu_Content_red, g_settings.menu_Content_green, g_settings.menu_Content_blue),
	                              convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue),
	                              8, convertSetupAlpha2Alpha(g_settings.menu_Content_alpha) );


	frameBuffer->paletteGenFade(COL_MENUCONTENTDARK,
	                              convertSetupColor2RGB(int(g_settings.menu_Content_red*0.6), int(g_settings.menu_Content_green*0.6), int(g_settings.menu_Content_blue*0.6)),
	                              convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue),
	                              8, convertSetupAlpha2Alpha(g_settings.menu_Content_alpha) );

	frameBuffer->paletteGenFade(COL_MENUCONTENTSELECTED,
	                              convertSetupColor2RGB(g_settings.menu_Content_Selected_red, g_settings.menu_Content_Selected_green, g_settings.menu_Content_Selected_blue),
	                              convertSetupColor2RGB(g_settings.menu_Content_Selected_Text_red, g_settings.menu_Content_Selected_Text_green, g_settings.menu_Content_Selected_Text_blue),
	                              8, convertSetupAlpha2Alpha(g_settings.menu_Content_Selected_alpha) );

	frameBuffer->paletteGenFade(COL_MENUCONTENTINACTIVE,
	                              convertSetupColor2RGB(g_settings.menu_Content_inactive_red, g_settings.menu_Content_inactive_green, g_settings.menu_Content_inactive_blue),
	                              convertSetupColor2RGB(g_settings.menu_Content_inactive_Text_red, g_settings.menu_Content_inactive_Text_green, g_settings.menu_Content_inactive_Text_blue),
	                              8, convertSetupAlpha2Alpha(g_settings.menu_Content_inactive_alpha) );

	frameBuffer->paletteGenFade(COL_INFOBAR,
	                              convertSetupColor2RGB(g_settings.infobar_red, g_settings.infobar_green, g_settings.infobar_blue),
	                              convertSetupColor2RGB(g_settings.infobar_Text_red, g_settings.infobar_Text_green, g_settings.infobar_Text_blue),
	                              8, convertSetupAlpha2Alpha(g_settings.infobar_alpha) );

	frameBuffer->paletteGenFade(COL_INFOBAR_SHADOW,
	                              convertSetupColor2RGB(int(g_settings.infobar_red*0.4), int(g_settings.infobar_green*0.4), int(g_settings.infobar_blue*0.4)),
	                              convertSetupColor2RGB(g_settings.infobar_Text_red, g_settings.infobar_Text_green, g_settings.infobar_Text_blue),
	                              8, convertSetupAlpha2Alpha(g_settings.infobar_alpha) );


	frameBuffer->paletteGenFade(COL_COLORED_EVENTS_INFOBAR,
	                              convertSetupColor2RGB(g_settings.infobar_red, g_settings.infobar_green, g_settings.infobar_blue),
	                              convertSetupColor2RGB(g_settings.colored_events_red, g_settings.colored_events_green, g_settings.colored_events_blue),
	                              8, convertSetupAlpha2Alpha(g_settings.infobar_alpha) );

	frameBuffer->paletteGenFade(COL_COLORED_EVENTS_CHANNELLIST,
	                              convertSetupColor2RGB(int(g_settings.menu_Content_red*0.6), int(g_settings.menu_Content_green*0.6), int(g_settings.menu_Content_blue*0.6)),
	                              convertSetupColor2RGB(g_settings.colored_events_red, g_settings.colored_events_green, g_settings.colored_events_blue),
	                              8, convertSetupAlpha2Alpha(g_settings.infobar_alpha) );

	// ##### TEXT COLORS #####
	// COL_COLORED_EVENTS_TEXT
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 0,
	                              convertSetupColor2RGB(g_settings.colored_events_red, g_settings.colored_events_green, g_settings.colored_events_blue),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_alpha));

	// COL_INFOBAR_TEXT
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 1,
	                              convertSetupColor2RGB(g_settings.infobar_Text_red, g_settings.infobar_Text_green, g_settings.infobar_Text_blue),
	                              convertSetupAlpha2Alpha(g_settings.infobar_alpha));

	// COL_INFOBAR_SHADOW_TEXT
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 2,
	                              convertSetupColor2RGB(int(g_settings.infobar_Text_red*0.6), int(g_settings.infobar_Text_green*0.6), int(g_settings.infobar_Text_blue*0.6)),
	                              convertSetupAlpha2Alpha(g_settings.infobar_alpha));

	// COL_MENUHEAD_TEXT
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 3,
	                              convertSetupColor2RGB(g_settings.menu_Head_Text_red, g_settings.menu_Head_Text_green, g_settings.menu_Head_Text_blue),
	                              convertSetupAlpha2Alpha(g_settings.menu_Head_alpha));

	// COL_MENUCONTENT_TEXT
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 4,
	                              convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_alpha));

	// COL_MENUCONTENT_TEXT_PLUS_1
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 5,
	                              changeBrightnessRGBRel(convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue), -16),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_alpha));

	// COL_MENUCONTENT_TEXT_PLUS_2
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 6,
	                              changeBrightnessRGBRel(convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue), -32),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_alpha));

	// COL_MENUCONTENT_TEXT_PLUS_3
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 7,
	                              changeBrightnessRGBRel(convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue), -48),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_alpha));

	// COL_MENUCONTENTDARK_TEXT
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 8,
	                              convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_alpha));

	// COL_MENUCONTENTDARK_TEXT_PLUS_1
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 9,
	                              changeBrightnessRGBRel(convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue), -52),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_alpha));

	// COL_MENUCONTENTDARK_TEXT_PLUS_2
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 10,
	                              changeBrightnessRGBRel(convertSetupColor2RGB(g_settings.menu_Content_Text_red, g_settings.menu_Content_Text_green, g_settings.menu_Content_Text_blue), -60),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_alpha));

	// COL_MENUCONTENTSELECTED_TEXT
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 11,
	                              convertSetupColor2RGB(g_settings.menu_Content_Selected_Text_red, g_settings.menu_Content_Selected_Text_green, g_settings.menu_Content_Selected_Text_blue),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_Selected_alpha));

	// COL_MENUCONTENTSELECTED_TEXT_PLUS_1
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 12,
	                              changeBrightnessRGBRel(convertSetupColor2RGB(g_settings.menu_Content_Selected_Text_red, g_settings.menu_Content_Selected_Text_green, g_settings.menu_Content_Selected_Text_blue), -16),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_Selected_alpha));

	// COL_MENUCONTENTSELECTED_TEXT_PLUS_2
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 13,
	                              changeBrightnessRGBRel(convertSetupColor2RGB(g_settings.menu_Content_Selected_Text_red, g_settings.menu_Content_Selected_Text_green, g_settings.menu_Content_Selected_Text_blue), -32),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_Selected_alpha));

	// COL_MENUCONTENTINACTIVE_TEXT
	frameBuffer->paletteSetColor(COL_NEUTRINO_TEXT + 14,
	                              convertSetupColor2RGB(g_settings.menu_Content_inactive_Text_red, g_settings.menu_Content_inactive_Text_green, g_settings.menu_Content_inactive_Text_blue),
	                              convertSetupAlpha2Alpha(g_settings.menu_Content_inactive_alpha));

	frameBuffer->paletteSet();
}

bool CColorSetupNotifier::changeNotify(const neutrino_locale_t, void *)
{
	setPalette();
	return false;
}

#if HAVE_SPARK_HARDWARE
CAudioSetupNotifier::CAudioSetupNotifier(void)
{
	mixerAnalog = mixerHDMI = mixerSPDIF = NULL;
}

CAudioSetupNotifier::~CAudioSetupNotifier(void)
{
	closeMixers();
}

void CAudioSetupNotifier::openMixers(void)
{
	if (!mixerAnalog)
		mixerAnalog = new mixerVolume("Analog", "1");
	if (!mixerHDMI)
		mixerHDMI = new mixerVolume("HDMI", "1");
	if (!mixerSPDIF)
		mixerSPDIF = new mixerVolume("SPDIF", "1");
}

void CAudioSetupNotifier::closeMixers(void)
{
	if (mixerAnalog)
		delete mixerAnalog;
	if (mixerHDMI)
		delete mixerHDMI;
	if (mixerSPDIF)
		delete mixerSPDIF;
	mixerAnalog = mixerHDMI = mixerSPDIF = NULL;
}

bool CAudioSetupNotifier::changeNotify(const neutrino_locale_t OptionName, void *data)
#else
bool CAudioSetupNotifier::changeNotify(const neutrino_locale_t OptionName, void *)
#endif
{
	//printf("notify: %d\n", OptionName);
#if 0 //FIXME to do ? manual audio delay
	if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_PCMOFFSET))
	{
	}
#endif
	if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_ANALOG_MODE)) {
		g_Zapit->setAudioMode(g_settings.audio_AnalogMode);
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_ANALOG_OUT)) {
		audioDecoder->EnableAnalogOut(g_settings.analog_out ? true : false);
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_HDMI_DD)) {
		audioDecoder->SetHdmiDD((HDMI_ENCODED_MODE) g_settings.hdmi_dd);
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_SPDIF_DD)) {
		audioDecoder->SetSpdifDD(g_settings.spdif_dd ? true : false);
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_AVSYNC)) {
		videoDecoder->SetSyncMode((AVSYNC_TYPE)g_settings.avsync);
		audioDecoder->SetSyncMode((AVSYNC_TYPE)g_settings.avsync);
		videoDemux->SetSyncMode((AVSYNC_TYPE)g_settings.avsync);
		audioDemux->SetSyncMode((AVSYNC_TYPE)g_settings.avsync);
		pcrDemux->SetSyncMode((AVSYNC_TYPE)g_settings.avsync);
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_CLOCKREC)) {
		//.Clock recovery enable/disable
		// FIXME add code here.
#if HAVE_SPARK_HARDWARE
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_MIXER_VOLUME_ANALOG)) {
		if (mixerAnalog)
			mixerAnalog->setVolume((long)(*((int *)(data))));
		else
			mixerVolume m("Analog", "1", (long)(*((int *)(data))));
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_MIXER_VOLUME_HDMI)) {
		if (mixerHDMI)
			mixerHDMI->setVolume((long)(*((int *)(data))));
		else
			mixerVolume m("HDMI", "1", (long)(*((int *)(data))));
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIOMENU_MIXER_VOLUME_SPDIF)) {
		if (mixerSPDIF)
			mixerSPDIF->setVolume((long)(*((int *)(data))));
		else
			mixerVolume m("SPDIF", "1", (long)(*((int *)(data))));
#endif
	} else if (ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIO_SRS_ALGO) ||
			ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIO_SRS_NMGR) ||
			ARE_LOCALES_EQUAL(OptionName, LOCALE_AUDIO_SRS_VOLUME)) {
		audioDecoder->SetSRS(g_settings.srs_enable, g_settings.srs_nmgr_enable, g_settings.srs_algo, g_settings.srs_ref_volume);
	}
	return false;
}

// used in ./gui/osd_setup.cpp:
bool CTimingSettingsNotifier::changeNotify(const neutrino_locale_t OptionName, void *)
{
	for (int i = 0; i < SNeutrinoSettings::TIMING_SETTING_COUNT; i++)
	{
		if (ARE_LOCALES_EQUAL(OptionName, timing_setting[i].name))
		{
			g_settings.timing[i] = 	atoi(g_settings.timing_string[i]);
			return true;
		}
	}
	return false;
}

// used in ./gui/osd_setup.cpp:
bool CFontSizeNotifier::changeNotify(const neutrino_locale_t, void *)
{
	CHintBox hintBox(LOCALE_MESSAGEBOX_INFO, g_Locale->getText(LOCALE_FONTSIZE_HINT)); // UTF-8
	hintBox.paint();

	CNeutrinoApp::getInstance()->SetupFonts(CNeutrinoFonts::FONTSETUP_NEUTRINO_FONT);

	hintBox.hide();
	/* recalculate infoclock/muteicon/volumebar */
	CVolumeHelper::getInstance()->refresh();
	return true;
}

int CSubtitleChangeExec::exec(CMenuTarget* /*parent*/, const std::string & actionKey)
{
printf("CSubtitleChangeExec::exec: action %s\n", actionKey.c_str());

	CMoviePlayerGui *mp = &CMoviePlayerGui::getInstance();
	bool is_mp = mp->Playing();

	if(actionKey == "off") {
		tuxtx_stop_subtitle();
		if (!mp && dvbsub_getpid() > 0)
			dvbsub_stop();
		if (is_mp && playback) {
			if (playback->GetDvbsubtitlePid() > 0)
				dvbsub_stop();
			playback->SetDvbsubtitlePid(-1);
			playback->SetSubtitlePid(-1);
			playback->SetTeletextPid(-1);
			mp->setCurrentTTXSub("");
		}
		return menu_return::RETURN_EXIT;
	}
	if(!strncmp(actionKey.c_str(), "DVB", 3)) {
		char const * pidptr = strchr(actionKey.c_str(), ':');
		int pid = atoi(pidptr+1);
		tuxtx_stop_subtitle();
		dvbsub_stop();
		if (is_mp) {
			playback->SetDvbsubtitlePid(-1);
			playback->SetSubtitlePid(-1);
			playback->SetTeletextPid(-1);
			mp->setCurrentTTXSub("");
			playback->SetDvbsubtitlePid(pid);
#if HAVE_SPARK_HARDWARE
			dvbsub_start(pid, true);
#else
			dvbsub_start(pid);
#endif
		} else {
			dvbsub_start(pid);
		}
	} else if(!strncmp(actionKey.c_str(), "TTX", 3)){
		char const * ptr = strchr(actionKey.c_str(), ':');
		ptr++;
		int pid = atoi(ptr);
		ptr = strchr(ptr, ':');
		ptr++;
		int page = strtol(ptr, NULL, 16);
		ptr = strchr(ptr, ':');
		ptr++;
//printf("CSubtitleChangeExec::exec: TTX, pid %x page %x lang %s\n", pid, page, ptr);
		tuxtx_stop_subtitle();
		dvbsub_stop();
		if (is_mp) {
			playback->SetDvbsubtitlePid(-1);
			playback->SetSubtitlePid(-1);
			playback->SetTeletextPid(pid);
			tuxtx_set_pid(pid, page, ptr);
#if HAVE_SPARK_HARDWARE
			tuxtx_main(pid, page, 0, true);
#else
			tuxtx_main(pid, page, 0);
#endif
			mp->setCurrentTTXSub(actionKey.c_str());
		} else {
			tuxtx_set_pid(pid, page, ptr);
			tuxtx_main(pid, page);
		}
	} else if(is_mp && !strncmp(actionKey.c_str(), "SUB", 3)){
		tuxtx_stop_subtitle();
		dvbsub_stop();
		playback->SetDvbsubtitlePid(-1);
		playback->SetSubtitlePid(-1);
		playback->SetTeletextPid(-1);
		mp->setCurrentTTXSub("");
		char const * pidptr = strchr(actionKey.c_str(), ':');
		int pid = atoi(pidptr+1);
		playback->SetSubtitlePid(pid);
	}
        return menu_return::RETURN_EXIT;
}

int CNVODChangeExec::exec(CMenuTarget* parent, const std::string & actionKey)
{
	//    printf("CNVODChangeExec exec: %s\n", actionKey.c_str());
	unsigned sel= atoi(actionKey);
	g_RemoteControl->setSubChannel(sel);

	parent->hide();
	g_InfoViewer->showSubchan();
	return menu_return::RETURN_EXIT;
}

int CStreamFeaturesChangeExec::exec(CMenuTarget* parent, const std::string & actionKey)
{
	//printf("CStreamFeaturesChangeExec exec: %s\n", actionKey.c_str());
	int sel= atoi(actionKey);

	if(parent != NULL)
		parent->hide();
	// -- obsolete (rasc 2004-06-10)
	// if (sel==-1)
	// {
	// 	CStreamInfo StreamInfo;
	//	StreamInfo.exec(NULL, "");
	// } else
	if(actionKey == "teletext") {
		g_RCInput->postMsg(CRCInput::RC_timeout, 0);
		g_RCInput->postMsg(CRCInput::RC_text, 0);
#if 0
		g_RCInput->clearRCMsg();
		tuxtx_main(frameBuffer->getFrameBufferPointer(), g_RemoteControl->current_PIDs.PIDs.vtxtpid);
		frameBuffer->paintBackground();
		if(!g_settings.cacheTXT)
			tuxtxt_stop();
		g_RCInput->clearRCMsg();
#endif
	}
	else if (sel>=0)
	{
		g_PluginList->startPlugin(sel,0);
	}

	return menu_return::RETURN_EXIT;
}

int CMoviePluginChangeExec::exec(CMenuTarget* parent, const std::string & actionKey)
{
	int sel= atoi(actionKey);
	parent->hide();
	if (sel>=0)
	{
			g_settings.movieplayer_plugin=g_PluginList->getName(sel);
	}
	return menu_return::RETURN_EXIT;
}
int COnekeyPluginChangeExec::exec(CMenuTarget* parent, const std::string & actionKey)
{
	int sel= atoi(actionKey);
	parent->hide();
	if (sel>=0)
	{
			g_settings.onekey_plugin=g_PluginList->getName(sel);
	}
	return menu_return::RETURN_EXIT;
}

long CNetAdapter::mac_addr_sys ( u_char *addr) //only for function getMacAddr()
{
	struct ifreq ifr;
	struct ifreq *IFR;
	struct ifconf ifc;
	char buf[1024];
	int s, i;
	int ok = 0;
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s==-1) 
	{
		return -1;
	}

	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	ioctl(s, SIOCGIFCONF, &ifc);
	IFR = ifc.ifc_req;
	for (i = ifc.ifc_len / sizeof(struct ifreq); --i >= 0; IFR++)
	{
		cstrncpy(ifr.ifr_name, IFR->ifr_name, sizeof(ifr.ifr_name));
		if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) 
		{
			if (! (ifr.ifr_flags & IFF_LOOPBACK)) 
			{
				if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) 
				{
					ok = 1;
					break;
				}
			}
		}
	}
	close(s);
	if (ok)
	{
		memmove(addr, ifr.ifr_hwaddr.sa_data, 6);
	}
	else
	{
		return -1;
	}
	return 0;
}

std::string CNetAdapter::getMacAddr(void)
{
	long stat;
	u_char addr[6];
	stat = mac_addr_sys( addr);
	if (0 == stat)
	{
		std::stringstream mac_tmp;
		for(int i=0;i<6;++i)
		mac_tmp<<std::hex<<std::setfill('0')<<std::setw(2)<<(int)addr[i]<<':';
		return mac_tmp.str().substr(0,17);
	}
	else
	{
		printf("[neutrino] eth-id not detected...\n");
		return "";
	}
}

bool CTZChangeNotifier::changeNotify(const neutrino_locale_t, void * Data)
{
	bool found = false;
	std::string name, zone;
	printf("CTZChangeNotifier::changeNotify: %s\n", (char *) Data);

        xmlDocPtr parser = parseXmlFile("/etc/timezone.xml");
        if (parser != NULL) {
                xmlNodePtr search = xmlDocGetRootElement(parser)->xmlChildrenNode;
                while (search) {
                        if (!strcmp(xmlGetName(search), "zone")) {
				name = xmlGetAttribute(search, "name");
				if(g_settings.timezone == name) {
					zone = xmlGetAttribute(search, "zone");
					if (!access("/usr/share/zoneinfo/" + zone, R_OK))
						found = true;
					break;
				}
                        }
                        search = search->xmlNextNode;
                }
                xmlFreeDoc(parser);
        }
	if(found) {
		printf("Timezone: %s -> %s\n", name.c_str(), zone.c_str());
		std::string cmd = "/usr/share/zoneinfo/" + zone;
		printf("symlink %s to /etc/localtime\n", cmd.c_str());
		if (unlink("/etc/localtime"))
			perror("unlink failed");
		if (symlink(cmd.c_str(), "/etc/localtime"))
			perror("symlink failed");
#if 0
		cmd = ":" + zone;
		setenv("TZ", cmd.c_str(), 1);
#endif
		tzset();
	}

	return false;
}

extern Zapit_config zapitCfg;

int CDataResetNotifier::exec(CMenuTarget* /*parent*/, const std::string& actionKey)
{
	bool delete_all = (actionKey == "all");
	bool delete_chan = (actionKey == "channels") || delete_all;
	bool delete_set = (actionKey == "settings") || delete_all;
	bool delete_removed = (actionKey == "delete_removed");
	neutrino_locale_t msg = delete_all ? LOCALE_RESET_ALL : delete_chan ? LOCALE_RESET_CHANNELS : LOCALE_RESET_SETTINGS;
	int ret = menu_return::RETURN_REPAINT;

	/* no need to confirm if we only remove deleted channels */
	if (!delete_removed) {
		int result = ShowMsg(msg, g_Locale->getText(LOCALE_RESET_CONFIRM), CMessageBox::mbrNo, CMessageBox::mbYes | CMessageBox::mbNo);
		if (result != CMessageBox::mbrYes)
			return true;
	}

	if(delete_all) {
		my_system(3, "/bin/sh", "-c", "rm -f " CONFIGDIR "/zapit/*.conf");
		CServiceManager::getInstance()->SatelliteList().clear();
		CZapit::getInstance()->LoadSettings();
		CZapit::getInstance()->GetConfig(zapitCfg);
		g_RCInput->postMsg( NeutrinoMessages::REBOOT, 0);
		ret = menu_return::RETURN_EXIT_ALL;
	}
	if(delete_set) {
		unlink(NEUTRINO_SETTINGS_FILE);
		//unlink(NEUTRINO_SCAN_SETTINGS_FILE);
		CNeutrinoApp::getInstance()->loadSetup(NEUTRINO_SETTINGS_FILE);
		CNeutrinoApp::getInstance()->saveSetup(NEUTRINO_SETTINGS_FILE);
		//CNeutrinoApp::getInstance()->loadColors(NEUTRINO_SETTINGS_FILE);
		CNeutrinoApp::getInstance()->SetupFonts();
		CNeutrinoApp::getInstance()->SetupTiming();
		CColorSetupNotifier::setPalette();
		CVFD::getInstance()->setlcdparameter();
		CFrameBuffer::getInstance()->Clear();
	}
	if(delete_chan) {
		my_system(3, "/bin/sh", "-c", "rm -f " CONFIGDIR "/zapit/*.xml");
		g_Zapit->reinitChannels();
	}
	if (delete_removed) {
		if (reloadhintBox)
			reloadhintBox->paint();
		CServiceManager::getInstance()->SaveServices(true, false, true);
		if (reloadhintBox)
			reloadhintBox->hide(); /* reinitChannels also triggers a reloadhintbox */
		g_Zapit->reinitChannels();
	}
	return ret;
}

#if HAVE_COOL_HARDWARE
void CFanControlNotifier::setSpeed(unsigned int speed)
{
	printf("FAN Speed %d\n", speed);
#ifndef BOXMODEL_APOLLO
	int cfd = open("/dev/cs_control", O_RDONLY);
	if(cfd < 0) {
		perror("Cannot open /dev/cs_control");
		return;
	}
	if (ioctl(cfd, IOC_CONTROL_PWM_SPEED, speed) < 0)
		perror("IOC_CONTROL_PWM_SPEED");

	close(cfd);
#endif
}

bool CFanControlNotifier::changeNotify(const neutrino_locale_t, void * data)
{
	unsigned int speed = * (int *) data;
	setSpeed(speed);
	return false;
}
#else
void CFanControlNotifier::setSpeed(unsigned int)
{
}

bool CFanControlNotifier::changeNotify(const neutrino_locale_t, void *)
{
	return false;
}
#endif

bool CCpuFreqNotifier::changeNotify(const neutrino_locale_t, void * data)
{
extern cCpuFreqManager * cpuFreq;
	int freq = * (int *) data;

	printf("CCpuFreqNotifier: %d Mhz\n", freq);
	freq *= 1000*1000;

	cpuFreq->SetCpuFreq(freq);
	return false;
}

extern CMenuOptionChooser::keyval_ext VIDEOMENU_VIDEOMODE_OPTIONS[];
bool CAutoModeNotifier::changeNotify(const neutrino_locale_t /*OptionName*/, void * /* data */)
{
	int i;
	int modes[VIDEO_STD_MAX+1];

	memset(modes, 0, sizeof(modes));

	for(i = 0; i < VIDEOMENU_VIDEOMODE_OPTION_COUNT; i++) {
		if (VIDEOMENU_VIDEOMODE_OPTIONS[i].key < 0) /* not available on this platform */
			continue;
		if (VIDEOMENU_VIDEOMODE_OPTIONS[i].key >= VIDEO_STD_MAX) {
			/* this must not happen */
			printf("CAutoModeNotifier::changeNotify VIDEOMODE_OPTIONS[%d].key = %d (>= %d)\n",
					i, VIDEOMENU_VIDEOMODE_OPTIONS[i].key, VIDEO_STD_MAX);
			continue;
		}
		modes[VIDEOMENU_VIDEOMODE_OPTIONS[i].key] = g_settings.enabled_video_modes[i];
	}
	videoDecoder->SetAutoModes(modes);
	return false;
}
