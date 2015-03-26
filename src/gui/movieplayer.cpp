/*
  Neutrino-GUI  -   DBoxII-Project

  Movieplayer (c) 2003, 2004 by gagga
  Based on code by Dirch, obi and the Metzler Bros. Thanks.
  (C) 2011 Stefan Seyfried

  Copyright (C) 2011 CoolStream International Ltd

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define __STDC_CONSTANT_MACROS
#include <global.h>
#include <neutrino.h>

#include <gui/audiomute.h>
#include <gui/audio_select.h>
#include <gui/epgview.h>
#include <gui/eventlist.h>
#include <gui/movieplayer.h>
#include <gui/infoviewer.h>
#include <gui/timeosd.h>
#include <gui/widget/helpbox.h>
#include <gui/infoclock.h>
#include <gui/plugins.h>
#include <gui/videosettings.h>
#include <driver/screenshot.h>
#include <driver/volume.h>
#include <driver/display.h>
#include <driver/abstime.h>
#include <driver/record.h>
#include <eitd/edvbstring.h>
#include <system/helpers.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/timeb.h>
#include <sys/mount.h>

#include <eitd/edvbstring.h>
#include <video.h>
#include <libtuxtxt/teletext.h>
#include <zapit/zapit.h>
#include <system/set_threadname.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <libdvbsub/dvbsub.h>
#include <audio.h>
#include <driver/nglcd.h>
#include <gui/widget/stringinput_ext.h>
#include <gui/screensetup.h>
#include <system/set_threadname.h>
#include <OpenThreads/ScopedLock>

#if HAVE_SPARK_HARDWARE
#include <libavcodec/avcodec.h>
#endif

//extern CPlugins *g_PluginList;
#ifndef HAVE_COOL_HARDWARE
#define LCD_MODE CVFD::MODE_MOVIE
#else
#define LCD_MODE CVFD::MODE_MENU_UTF8
#endif

extern CRemoteControl *g_RemoteControl;	/* neutrino.cpp */
extern CInfoClock *InfoClock;

#define TIMESHIFT_SECONDS 3
#define ISO_MOUNT_POINT "/media/iso"

CMoviePlayerGui* CMoviePlayerGui::instance_mp = NULL;

CMoviePlayerGui& CMoviePlayerGui::getInstance()
{
	if (!instance_mp)
	{
		instance_mp = new CMoviePlayerGui();
		printf("[neutrino CMoviePlayerGui] Instance created...\n");
	}
	return *instance_mp;
}

CMoviePlayerGui::CMoviePlayerGui()
{
	Init();
}

CMoviePlayerGui::~CMoviePlayerGui()
{
	PlayFileEnd();
	delete moviebrowser;
	delete filebrowser;
	delete bookmarkmanager;
	delete playback;
	instance_mp = NULL;
}

#if !HAVE_COOL_HARDWARE
// used by libdvbsub/dvbsub.cpp
void getPlayerPts(int64_t *pts)
{
	cPlayback *playback = CMoviePlayerGui::getInstance().getPlayback();
	if (playback)
		playback->GetPts((uint64_t &) *pts);
}
#endif

void CMoviePlayerGui::Init(void)
{
	playing = false;
	playback = NULL;

	frameBuffer = CFrameBuffer::getInstance();

	moviebrowser = new CMovieBrowser();
	bookmarkmanager = new CBookmarkManager();

	const char *filters[] = {
		"ts", "mpg", "mpeg", "m2p", "mpv", "vob", "m2ts", "mp4", "mov", "m3u", "pls",
#if HAVE_TRIPLEDRAGON
		"vdr",
#else
		"avi", "mkv", "wav", "asf", "aiff",
#endif
#if HAVE_SPARK_HARDWARE
		"vdr", "flv", "wmv", "iso",
#endif
		NULL
	};

	for (const char **f = filters; *f; f++)
		tsfilefilter.addFilter(*f);

	if (g_settings.network_nfs_moviedir.empty())
		Path_local = "/";
	else
		Path_local = g_settings.network_nfs_moviedir;

	if (g_settings.filebrowser_denydirectoryleave)
		filebrowser = new CFileBrowser(Path_local.c_str());
	else
		filebrowser = new CFileBrowser();

	filebrowser->Filter = &tsfilefilter;
	filebrowser->Hide_records = true;

	speed = 1;
	timeshift = TSHIFT_MODE_OFF;
	numpida = 0;
	numpids = 0;
	numpidt = 0;
	showStartingHint = false;

	filelist_it = filelist.end();

	StreamType = AUDIO_FMT_AUTO;
	hintBox = NULL;
	playback = new cPlayback(3);
	stopped = true;
	iso_file = false;
}

void CMoviePlayerGui::cutNeutrino()
{
	if (playing)
		return;

	playing = true;
	/* set g_InfoViewer update timer to 1 sec, should be reset to default from restoreNeutrino->set neutrino mode  */
	//g_InfoViewer->setUpdateTimer(1000 * 1000);

	if (isUPNP)
		return;

	CZapit::getInstance()->lockPlayBack();
#if 0
	g_Sectionsd->setPauseScanning(true);
#endif

#ifdef HAVE_AZBOX_HARDWARE
	/* we need sectionsd to get idle and zapit to release the demuxes
	 * and decoders so that the external player can do its work
	 * TODO: what about timeshift? */
	g_Sectionsd->setServiceChanged(0, false);
	CZapit::getInstance()->setStandby(true);
#endif

	m_LastMode = (CNeutrinoApp::getInstance()->getMode() | NeutrinoMessages::norezap);
	CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::CHANGEMODE, isWebTV ? NeutrinoMessages::mode_webtv : NeutrinoMessages::mode_ts);
}

void CMoviePlayerGui::restoreNeutrino()
{
	if (!playing)
		return;

	playing = false;
#ifdef HAVE_AZBOX_HARDWARE
	CZapit::getInstance()->setStandby(false);
	CZapit::getInstance()->SetVolume(CZapit::getInstance()->GetVolume());
#endif

	if (isUPNP)
		return;

	CZapit::getInstance()->unlockPlayBack();
#if 0
	g_Sectionsd->setPauseScanning(false);
#endif
	//CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::EVT_PROGRAMLOCKSTATUS, (neutrino_msg_data_t) 0x200);
	if (m_LastMode == NeutrinoMessages::mode_tv)
		g_RCInput->postMsg(NeutrinoMessages::EVT_PROGRAMLOCKSTATUS, 0x200, false);

	if (m_LastMode != NeutrinoMessages::mode_unknown)
		CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::CHANGEMODE, m_LastMode);
	if (m_LastMode == NeutrinoMessages::mode_tv) {
		CZapitChannel *channel = CZapit::getInstance()->GetCurrentChannel();
		if (channel && channel->scrambled)
			CZapit::getInstance()->Rezap();
	}
	CVFD::getInstance()->setAudioMode();
}

static bool running = false;
int CMoviePlayerGui::exec(CMenuTarget * parent, const std::string & actionKey)
{
	printf("[movieplayer] actionKey=%s\n", actionKey.c_str());
	if (running)
		return menu_return::RETURN_EXIT_ALL;
	running = true;

	if (parent)
		parent->hide();

	if (!access(MOVIEPLAYER_START_SCRIPT, X_OK)) {
		puts("[movieplayer.cpp] executing " MOVIEPLAYER_START_SCRIPT ".");
		if (my_system(MOVIEPLAYER_START_SCRIPT) != 0)
			perror(MOVIEPLAYER_START_SCRIPT " failed");
	}
	Cleanup();
	
	isMovieBrowser = false;
	isWebTV = false;
	isYT = false;
	isNK = false;
	isBookmark = false;
	timeshift = TSHIFT_MODE_OFF;
	isHTTP = false;
	isUPNP = false;

	if (actionKey == "tsmoviebrowser") {
		isMovieBrowser = true;
		moviebrowser->setMode(MB_SHOW_RECORDS);
	}
	else if (actionKey == "ytplayback") {
		frameBuffer->Clear();
		CAudioMute::getInstance()->enableMuteIcon(false);
		InfoClock->enableInfoClock(false);
		isMovieBrowser = true;
		moviebrowser->setMode(MB_SHOW_YT);
		isYT = true;
	}
	else if (actionKey == "nkplayback") {
		frameBuffer->Clear();
		CAudioMute::getInstance()->enableMuteIcon(false);
		isMovieBrowser = true;
		moviebrowser->setMode(MB_SHOW_NK);
		isNK = true;
	}
	else if (actionKey == "fileplayback") {
	}
	else if (actionKey == "timeshift") {
		timeshift = TSHIFT_MODE_TEMPORARY;
	}
	else if (actionKey == "ptimeshift") {
		timeshift = TSHIFT_MODE_PERMANENT;
	}
	else if (actionKey == "rtimeshift") {
		timeshift = TSHIFT_MODE_PAUSE;
	}
#if 0 // TODO ?
	else if (actionKey == "bookmarkplayback") {
		isBookmark = true;
	}
#endif
	else if (actionKey == "netstream")
	{
		isHTTP = true;
		file_name = g_settings.streaming_server_url;
		pretty_name = (isWebTV ? g_settings.streaming_server_name : g_settings.streaming_server_url);
		p_movie_info = NULL;
		PlayFile();
	} else if (actionKey == "upnp") {
		isUPNP = true;
		PlayFile();
	}
	else if (actionKey == "http") {
		isHTTP = true;
		PlayFile();
	}
	else {
		running = false;
		return menu_return::RETURN_REPAINT;
	}

	repeat_mode = REPEAT_OFF;

	std::string oldservicename = CVFD::getInstance()->getServicename();
	while(!isHTTP && !isUPNP && SelectFile()) {
		CVFD::getInstance()->setMode(CVFD::MODE_TVRADIO);
		if (isWebTV || isYT || isNK)
			CVFD::getInstance()->showServicename(pretty_name.c_str());
		else
			CVFD::getInstance()->showServicename(file_name.c_str());
		if (timeshift != TSHIFT_MODE_OFF) {
			CVFD::getInstance()->ShowIcon(FP_ICON_TIMESHIFT, true);
			PlayFile();
			CVFD::getInstance()->ShowIcon(FP_ICON_TIMESHIFT, false);
			break;
		}
		do
			PlayFile();
		while (repeat_mode);
	}
	CVFD::getInstance()->showServicename(oldservicename.c_str());

	bookmarkmanager->flush();

	if (!access(MOVIEPLAYER_END_SCRIPT, X_OK)) {
		puts("[movieplayer.cpp] executing " MOVIEPLAYER_END_SCRIPT ".");
		if (my_system(MOVIEPLAYER_END_SCRIPT) != 0)
			perror(MOVIEPLAYER_END_SCRIPT " failed");
	}

	CVFD::getInstance()->setMode(CVFD::MODE_TVRADIO);

	running = false;

	if (isYT || isNK) {
		CAudioMute::getInstance()->enableMuteIcon(true);
		InfoClock->enableInfoClock(true);
	}

	if (timeshift != TSHIFT_MODE_OFF){
		timeshift = TSHIFT_MODE_OFF;
		return menu_return::RETURN_EXIT_ALL;
	}
	return menu_ret; //menu_return::RETURN_REPAINT;
}

void CMoviePlayerGui::updateLcd()
{
#if !HAVE_SPARK_HARDWARE
	char tmp[20];
	std::string lcd;
	std::string name;

	if (isMovieBrowser && p_movie_info && strlen(p_movie_info->epgTitle.c_str()) && strncmp(p_movie_info->epgTitle.c_str(), "not", 3))
		name = p_movie_info->epgTitle;
	else
		name = pretty_name;

	switch (playstate) {
		case CMoviePlayerGui::PAUSE:
			if (speed < 0) {
				sprintf(tmp, "%dx<| ", abs(speed));
				lcd = tmp;
			} else if (speed > 0) {
				sprintf(tmp, "%dx|> ", abs(speed));
				lcd = tmp;
			} else
				lcd = "|| ";
			break;
		case CMoviePlayerGui::REW:
			sprintf(tmp, "%dx<< ", abs(speed));
			lcd = tmp;
			break;
		case CMoviePlayerGui::FF:
			sprintf(tmp, "%dx>> ", abs(speed));
			lcd = tmp;
			break;
		case CMoviePlayerGui::PLAY:
			lcd = "> ";
			break;
		default:
			break;
	}
	lcd += name;
	CVFD::getInstance()->setMode(LCD_MODE);
	CVFD::getInstance()->showMenuText(0, lcd.c_str(), -1, true);
#endif
}

void CMoviePlayerGui::fillPids()
{
	if (p_movie_info == NULL)
		return;

	numpida = 0; currentapid = 0;
	numpids = 0;
	numpidt = 0;
	currentttxsub = "";
	if (!p_movie_info->audioPids.empty()) {
		currentapid = p_movie_info->audioPids[0].epgAudioPid;
		currentac3 = p_movie_info->audioPids[0].atype;
	}
	for (unsigned int i = 0; i < p_movie_info->audioPids.size(); i++) {
		unsigned int j;
		for (j = 0; j < numpida && p_movie_info->audioPids[i].epgAudioPid != apids[j]; j++);
		if (j == numpida) {
			apids[i] = p_movie_info->audioPids[i].epgAudioPid;
			ac3flags[i] = p_movie_info->audioPids[i].atype;
			numpida++;
			if (p_movie_info->audioPids[i].selected) {
				currentapid = p_movie_info->audioPids[i].epgAudioPid;
				currentac3 = p_movie_info->audioPids[i].atype;
			}
			if (numpida == REC_MAX_APIDS)
				break;
		}
	}
	if (!p_movie_info->epgVideoPid)
		p_movie_info->epgVideoPid = playback->GetVPid();
	vpid = p_movie_info->epgVideoPid;
	vtype = p_movie_info->VideoType;
}

void CMoviePlayerGui::Cleanup()
{
	/*clear audiopids */
	for (unsigned int i = 0; i < REC_MAX_APIDS; i++) {
		apids[i] = 0;
		ac3flags[i] = 0;
		language[i].clear();
	}
	numpida = 0; currentapid = 0;
	// clear subtitlepids
	for (unsigned int i = 0; i < REC_MAX_SPIDS; i++) {
		spids[i] = 0;
		slanguage[i].clear();
	}
	numpids = 0;
	// clear teletextpids
	for (unsigned int i = 0; i < REC_MAX_TPIDS; i++) {
		tpids[i] = 0;
		tlanguage[i].clear();
	}
	numpidt = 0; currentttxsub = "";
	vpid = 0;
	vtype = 0;

	startposition = 0;
	p_movie_info = NULL;
}

void CMoviePlayerGui::makeFilename()
{
	if (pretty_name.empty()) {
		std::string::size_type pos = file_name.find_last_of('/');
		if (pos != std::string::npos) {
			pretty_name = file_name.substr(pos+1);
			std::replace(pretty_name.begin(), pretty_name.end(), '_', ' ');
		} else
			pretty_name = file_name;

		if (pretty_name.substr(0,14)=="videoplayback?") {//youtube name
			if (!p_movie_info->epgTitle.empty())
				pretty_name = p_movie_info->epgTitle;
			else
				pretty_name = "";
		}
		printf("CMoviePlayerGui::makeFilename: full_name [%s] pretty_name [%s]\n", file_name.c_str(), pretty_name.c_str());
	}
}

bool CMoviePlayerGui::SelectFile()
{
	bool ret = false;
	menu_ret = menu_return::RETURN_REPAINT;

	Cleanup();
	pretty_name = "";
	file_name = "";

	printf("CMoviePlayerGui::SelectFile: isBookmark %d timeshift %d isMovieBrowser %d\n", isBookmark, timeshift, isMovieBrowser);
	wakeup_hdd(g_settings.network_nfs_recordingdir.c_str());

	if (timeshift != TSHIFT_MODE_OFF) {
		t_channel_id live_channel_id = CZapit::getInstance()->GetCurrentChannelID();
		p_movie_info = CRecordManager::getInstance()->GetMovieInfo(live_channel_id);
		file_name = CRecordManager::getInstance()->GetFileName(live_channel_id) + ".ts";
		fillPids();
		ret = true;
	}
#if 0 // TODO
	else if (isBookmark) {
		const CBookmark * theBookmark = bookmarkmanager->getBookmark(NULL);
		if (theBookmark == NULL) {
			bookmarkmanager->flush();
			return false;
		}
		file_name = theBookmark->getUrl();
		sscanf(theBookmark->getTime(), "%lld", &startposition);
		startposition *= 1000;
		ret = true;
	}
#endif
	else if (isMovieBrowser) {
		if (moviebrowser->exec(Path_local.c_str())) {
			// get the current path and file name
			Path_local = moviebrowser->getCurrentDir();
			CFile *file;
			if ((file = moviebrowser->getSelectedFile()) != NULL) {
				// get the movie info handle (to be used for e.g. bookmark handling)
				p_movie_info = moviebrowser->getCurrentMovieInfo();
				if (moviebrowser->getMode() == MB_SHOW_RECORDS) {
					file_name = file->Name;
				}
				else if (isYT || isNK) {
					pretty_name = file->Name;
					file_name = file->Url;
				}
				fillPids();
				if (file->Name.empty()) {
					// reset pids for multi-program selection
					vpid = 0;
					currentapid = 0;
				}

				// get the start position for the movie
				startposition = 1000 * moviebrowser->getCurrentStartPos();
				printf("CMoviePlayerGui::SelectFile: file %s start %d apid %X atype %d vpid %x vtype %d\n", file_name.c_str(), startposition, currentapid, currentac3, vpid, vtype);

				ret = true;
			}
		} else
			menu_ret = moviebrowser->getMenuRet();
	} else if (filelist.size() > 0 && repeat_mode == REPEAT_TRACK) {
		--filelist_it;
		file_name = (*filelist_it).Name;
		++filelist_it;
		ret = true;
	} else if (filelist.size() > 0 && filelist_it == filelist.end() && repeat_mode == REPEAT_ALL) {
		filelist_it = filelist.begin();
		file_name = (*filelist_it).Name;
		++filelist_it;
		ret = true;
	} else if (filelist.size() > 0 && filelist_it != filelist.end()) {
		file_name = (*filelist_it).Name;
		++filelist_it;
		ret = true;
	} else { // filebrowser
		CAudioMute::getInstance()->enableMuteIcon(false);
		filebrowser->Multi_Select = g_settings.filebrowser_multi_select;
		InfoClock->enableInfoClock(false);
		if (filebrowser->exec(Path_local.c_str()) == true) {
			Path_local = filebrowser->getCurrentDir();
			CFile *file = filebrowser->getSelectedFile();
			filelist = filebrowser->getSelectedFiles();
			filelist_it = filelist.end();
			if (filelist.size() > 1) {
				filelist_it = filelist.begin();
				file_name = (*filelist_it).Name;
				++filelist_it;
				ret = true;
			} else if (file) {
				file_name = file->Name;
				ret = true;
				if(file->getType() == CFile::FILE_PLAYLIST)
					parsePlaylist(file);
				else if(file->getType() == CFile::FILE_ISO)
					ret = mountIso(file);
			}
		} else
			menu_ret = filebrowser->getMenuRet();
		CAudioMute::getInstance()->enableMuteIcon(true);
		InfoClock->enableInfoClock(true);
	}
	if(ret && pretty_name.empty())
		makeFilename();
	//store last multiformat play dir
	g_settings.network_nfs_moviedir = Path_local;

	return ret;
}

void *CMoviePlayerGui::ShowStartHint(void *arg)
{
	set_threadname(__func__);
	CMoviePlayerGui *caller = (CMoviePlayerGui *)arg;
	CHintBox *hb = NULL;
	if(!caller->pretty_name.empty() && (caller->isYT || caller->isNK || caller->isWebTV )){
		neutrino_locale_t title = caller->isYT ? LOCALE_MOVIEPLAYER_YTPLAYBACK : caller->isNK ? LOCALE_MOVIEPLAYER_NKPLAYBACK : LOCALE_WEBTV_HEAD;
		hb = new CHintBox(title, caller->pretty_name.c_str(), 450, NEUTRINO_ICON_STREAMING);
		hb->paint();
	}
	while (caller->showStartingHint) {
		neutrino_msg_t msg;
		neutrino_msg_data_t data;
		g_RCInput->getMsg(&msg, &data, 1);
		if (msg == CRCInput::RC_home || msg == CRCInput::RC_stop) {
			caller->playback->RequestAbort();
		} else if (caller->isWebTV) {
			CNeutrinoApp::getInstance()->handleMsg(msg, data);
		} else if (msg != CRCInput::RC_timeout && msg > CRCInput::RC_MaxRC) {
			CNeutrinoApp::getInstance()->handleMsg(msg, data);
		}
	}
	if(hb){
		hb->hide();
		delete hb;
	}
	return NULL;
}

void CMoviePlayerGui::PlayFile(void)
{
	mutex.lock();
	PlayFileStart();
	mutex.unlock();
	PlayFileLoop();
	PlayFileEnd();
}

void* CMoviePlayerGui::bgPlayThread(void *arg)
{
	set_threadname(__func__);
	CMoviePlayerGui *mp = (CMoviePlayerGui *) arg;

	while (mp->playback->IsPlaying())
		usleep(100000);
	mp->PlayFileEnd();
	pthread_exit(NULL);
}

bool CMoviePlayerGui::PlayBackgroundStart(const std::string &file, const std::string &name, t_channel_id chan)
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> m_lock(mutex);
	if (g_settings.parentallock_prompt != PARENTALLOCK_PROMPT_NEVER) {
		int age = -1;
		const char *ages[] = { "18+", "16+", "12+", "6+", "0+", NULL };
		int agen[] = { 18, 16, 12, 6, 0 };
		for (int i = 0; ages[i] && age < 0; i++) {
			const char *n = name.c_str();
			char *h = (char *) n;
			while ((age < 0) && (h = strstr(h, ages[i])))
				if ((h == n) || !isdigit(*(h - 1)))
					age = agen[i];
		}
		if (age > -1 && age >= g_settings.parentallock_lockage) {
			//CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::EVT_PROGRAMLOCKSTATUS, (neutrino_msg_data_t) age);
			g_RCInput->postMsg(NeutrinoMessages::EVT_PROGRAMLOCKSTATUS, age, false);
		}
	}

	isMovieBrowser = false;
	isWebTV = true;
	isYT = false;
	isNK = false;
	isBookmark = false;
	timeshift = TSHIFT_MODE_OFF;
	isHTTP = true;
	isUPNP = false;
	isHTTP = true;

	file_name = file;
	pretty_name = name;
	MI_MOVIE_INFO _mi;
	mi.epgTitle = name;
	mi.epgChannel = file;
	mi.epgId = chan;
	p_movie_info = &mi;

	numpida = 0; currentapid = 0;
	numpids = 0;
	numpidt = 0; currentttxsub = "";

	bool res = PlayFileStart();
	if (res) {
		if (pthread_create (&bgThread, 0, CMoviePlayerGui::bgPlayThread, this))
			fprintf(stderr, "ERROR: pthread_create(%s)\n", __func__);
		else
			pthread_detach(bgThread);
	}
	return res;
}

bool MoviePlayerZapto(const std::string &file, const std::string &name, t_channel_id chan)
{
	return CMoviePlayerGui::getInstance().PlayBackgroundStart(file, name, chan);
}

extern void MoviePlayerStop(void)
{
	CMoviePlayerGui::getInstance().stopPlayBack();
}

void CMoviePlayerGui::RequestAbort(void)
{
	ShowAbortHintBox();
	playback->RequestAbort();
#if HAVE_COOL_HARDWARE
	playback->Close();
#endif
	while (!stopped)
		usleep(100000);
	HideHintBox();
}

void CMoviePlayerGui::ShowAbortHintBox(void)
{
	if(!stopped && (isYT || isNK || isWebTV)) {
		if (hintBox) {
			hintBox->hide();
			delete hintBox;
		}
		neutrino_locale_t title = isYT ? LOCALE_MOVIEPLAYER_YTPLAYBACK : isNK ? LOCALE_MOVIEPLAYER_NKPLAYBACK : LOCALE_WEBTV_HEAD;
		hintBox = new CHintBox(title, g_Locale->getText(LOCALE_MOVIEPLAYER_STOPPING));
		hintBox->paint();
	}
}

void CMoviePlayerGui::HideHintBox(void)
{
	if (hintBox) {
		hintBox->hide();
		delete hintBox;
		hintBox = NULL;
	}
}

void CMoviePlayerGui::stopPlayBack(void)
{
	playback->RequestAbort();
#if HAVE_COOL_HARDWARE
	playback->Close();
#endif
	filelist.clear();
	repeat_mode = REPEAT_OFF;
	while (!stopped)
		usleep(100000);
}

static const char *ProgramSelectionCallback(void *, std::vector<std::string> &keys, std::vector<std::string> &values)
{
	CMenuWidgetSelection m(LOCALE_MOVIEBROWSER_PROGRAM_SELECTION , NEUTRINO_ICON_MOVIEPLAYER);
	m.enableFade(false);
	m.addIntroItems(NONEXISTANT_LOCALE, NONEXISTANT_LOCALE, CMenuWidget::BTN_TYPE_CANCEL);

	int off = m.getItemsCount();

	for (unsigned int i = 0; i < keys.size(); i++)
		m.addItem(new CMenuForwarder(values[i]));

	m.exec(NULL, "");

	int res = m.getSelectedLine() - off;

	if (res > -1 && res < (int) keys.size())
		return keys[res].c_str();

	m.hide();

	return NULL;
}

static const char *ProgramSelectionCallbackWebTV(void *, std::vector<std::string> &keys, std::vector<std::string> &)
{
	// FIXME
	return keys[keys.size() - 1].c_str();
}

bool CMoviePlayerGui::PlayFileStart(void)
{
	menu_ret = menu_return::RETURN_REPAINT;

	first_start_timeshift = false;
	update_lcd = true;

	//CTimeOSD FileTime;
	position = 0, duration = 0;
	speed = 1;

	bool _playing = playing;
	playing = false; // don't restore neutrino
	RequestAbort();
	playing = _playing;
	cutNeutrino();

	playstate = CMoviePlayerGui::STOPPED;
	printf("Startplay at %d seconds\n", startposition/1000);
	handleMovieBrowser(CRCInput::RC_nokey, position);

	playback->SetTeletextPid(-1);

	playback->Open(timeshift == TSHIFT_MODE_OFF ? PLAYMODE_FILE : PLAYMODE_TS);

	if (p_movie_info) {
		if (timeshift != TSHIFT_MODE_OFF) {
		// p_movie_info may be invalidated by CRecordManager while we're still using it. Create and use a copy.
			mi = *p_movie_info;
			p_movie_info = &mi;
		}

		duration = p_movie_info->length * 60 * 1000;
#if HAVE_SPARK_HARDWARE
		CScreenSetup cSS;
		cSS.showBorder(p_movie_info->epgId);
#endif
	} else {
#if HAVE_SPARK_HARDWARE
		CScreenSetup cSS;
		cSS.showBorder(0);
#endif
	}

	file_prozent = 0;
#if HAVE_SPARK_HARDWARE
	old3dmode = frameBuffer->get3DMode();
#endif
#ifdef ENABLE_GRAPHLCD
	nGLCD::MirrorOSD(false);
	if (p_movie_info)
		nGLCD::lockChannel(p_movie_info->epgChannel, p_movie_info->epgTitle);
#endif
	pthread_t thrStartHint = 0;
	if (isWebTV || isYT || isNK) {
		showStartingHint = true;
		pthread_create(&thrStartHint, NULL, CMoviePlayerGui::ShowStartHint, this);
	}

	if (isWebTV)
		playback->SetProgramSelectionCallback(ProgramSelectionCallbackWebTV, NULL);
	else
		playback->SetProgramSelectionCallback(ProgramSelectionCallback, NULL);

	bool res = playback->Start((char *) file_name.c_str(), vpid, vtype, currentapid, currentac3, duration);
	if (thrStartHint) {
		showStartingHint = false;
		pthread_join(thrStartHint, NULL);
	}

	if (!res) {
		stopped = false;
		PlayFileEnd(true);
	} else {
		if (!p_movie_info) {
			std::vector<std::string> keys, values;
			playback->GetMetadata(keys, values);
			size_t count = keys.size();
			if (count > 0) {
				mi.clear();
				for (size_t i = 0; i < count; i++) {
					std::string key = trim(keys[i]);
					if (mi.epgTitle.empty() && !strcasecmp("title", key.c_str())) {
 						mi.epgTitle = isUTF8(values[i]) ? values[i] : convertLatin1UTF8(values[i]);
						pretty_name = mi.epgTitle;
						CVFD::getInstance()->showServicename(pretty_name.c_str());
						continue;
					}
					if (mi.epgChannel.empty() && !strcasecmp("artist", key.c_str())) {
 						mi.epgChannel = isUTF8(values[i]) ? values[i] : convertLatin1UTF8(values[i]);
						continue;
					}
					if (mi.epgInfo1.empty() && !strcasecmp("comment", key.c_str())) {
 						mi.epgInfo1 = isUTF8(values[i]) ? values[i] : convertLatin1UTF8(values[i]);
						continue;
					}
				}
				if (!mi.epgChannel.empty() || !mi.epgTitle.empty())
					p_movie_info = &mi;
#ifdef ENABLE_GRAPHLCD
				if (p_movie_info)
					nGLCD::lockChannel(p_movie_info->epgChannel, p_movie_info->epgTitle);
#endif
			}
		}
		stopped = false;
		getAPIDCount();
		if (p_movie_info)
			for (unsigned int i = 0; i < numpida; i++) {
				unsigned int j, asize = p_movie_info->audioPids.size();
				for (j = 0; j < asize && p_movie_info->audioPids[j].epgAudioPid != apids[i]; j++);
				if (j == asize) {
					EPG_AUDIO_PIDS pids;
					pids.epgAudioPid = apids[i];
					pids.selected = 0;
					pids.atype = ac3flags[i];
					pids.epgAudioPidName = language[i];
					p_movie_info->audioPids.push_back(pids);
				}
			}

		if (!currentapid && numpida > 0) {
			currentapid = apids[0];
			currentac3 = ac3flags[0];
			SetStreamType();
		} else
			StreamType = AUDIO_FMT_AUTO;

		CVFD::getInstance()->setAudioMode(StreamType);

		if (p_movie_info && p_movie_info->epgId) {
			int percent = CZapit::getInstance()->GetPidVolume(p_movie_info->epgId, currentapid, currentac3 == 1);
			CZapit::getInstance()->SetVolumePercent(percent);
		}
		playstate = CMoviePlayerGui::PLAY;
		CVFD::getInstance()->ShowIcon(FP_ICON_PLAY, true);
		if (timeshift != TSHIFT_MODE_OFF) {
			first_start_timeshift = true;
			startposition = -1;
			int towait = (timeshift == TSHIFT_MODE_TEMPORARY) ? TIMESHIFT_SECONDS+1 : TIMESHIFT_SECONDS;
			for (unsigned int i = 0; i < 500; i++) {
				playback->GetPosition(position, duration);
				startposition = (duration - position);

				//printf("CMoviePlayerGui::PlayFile: waiting for data, position %d duration %d (%d), start %d\n", position, duration, towait, startposition);
				if(startposition > towait*1000)
					break;

				usleep(20000);
			}
			if (timeshift == TSHIFT_MODE_PAUSE) {
				startposition = duration;
			} else {
				if (g_settings.timeshift_pause)
					playstate = CMoviePlayerGui::PAUSE;
				if (timeshift == TSHIFT_MODE_TEMPORARY)
					startposition = 0;
				else
					startposition = duration - TIMESHIFT_SECONDS*1000;
			}
			printf("******************* Timeshift %d, position %d, seek to %d seconds\n", timeshift, position, startposition/1000);
		}
#if HAVE_COOL_HARDWARE
		if(timeshift != TSHIFT_MODE_OFF && startposition >= 0)//FIXME no jump for file at start yet
#else
		if(!isWebTV && startposition > -1)
#endif
			playback->SetPosition(startposition, true);

		/* playback->Start() starts paused */
		if (timeshift == TSHIFT_MODE_PAUSE) {
			speed = -1;
			playback->SetSpeed(-1);
			playstate = CMoviePlayerGui::REW;
			if (!FileTime.IsVisible() && !time_forced) {
				FileTime.switchMode(position, duration);
				time_forced = true;
			}
		} else if (timeshift == TSHIFT_MODE_OFF || !g_settings.timeshift_pause) {
			playback->SetSpeed(1);
		}
	}
	selectAutoLang();

	CAudioMute::getInstance()->enableMuteIcon(true);
	InfoClock->enableInfoClock(true);
	return res;
}

bool CMoviePlayerGui::SetPosition(int pos, bool absolute)
{
	StopSubtitles(true);
	bool res = playback->SetPosition(pos, absolute);
	StartSubtitles(true);
	return res;
}

void CMoviePlayerGui::PlayFileLoop(void)
{
	time_forced = false;
	update_lcd = true;
#if HAVE_COOL_HARDWARE
	int eof = 0;
#endif
	while (playstate >= CMoviePlayerGui::PLAY)
	{
#ifdef ENABLE_GRAPHLCD
		if (p_movie_info && !isWebTV)
			nGLCD::lockChannel(p_movie_info->epgChannel, p_movie_info->epgTitle, duration ? (100 * position / duration) : 0);
#endif
		if (update_lcd) {
			update_lcd = false;
			updateLcd();
		}
		if (first_start_timeshift) {
			callInfoViewer(/*duration, position*/);
			first_start_timeshift = false;
		}
		if (time_forced && (playstate != CMoviePlayerGui::FF) && (time_forced != CMoviePlayerGui::REW)) {
			FileTime.kill(time_forced);
			time_forced = false;
		}

		neutrino_msg_t msg;
		neutrino_msg_data_t data;
		g_RCInput->getMsg(&msg, &data, 10);	// 1 secs..

		if ((playstate >= CMoviePlayerGui::PLAY) && (timeshift != TSHIFT_MODE_OFF || (playstate != CMoviePlayerGui::PAUSE))) {
			if (playback->GetPosition(position, duration)) {
				if (time_forced)
					FileTime.show(position, true);
				else
					FileTime.update(position, duration);
				if (duration > 100)
					file_prozent = (unsigned char) (position / (duration / 100));
#if HAVE_TRIPLEDRAGON
				CVFD::getInstance()->showPercentOver(file_prozent, true, CVFD::MODE_MOVIE);
#else
				CVFD::getInstance()->showPercentOver(file_prozent);
#endif

				playback->GetSpeed(speed);
				/* at BOF lib set speed 1, check it */
				if ((playstate != CMoviePlayerGui::PLAY) && (speed == 1)) {
					playstate = CMoviePlayerGui::PLAY;
					update_lcd = true;
				}
			} else
#if HAVE_COOL_HARDWARE
			{
				/* in case ffmpeg report incorrect values */
				int posdiff = duration - position;
				if ((posdiff >= 0) && (posdiff < 2000) && timeshift == TSHIFT_MODE_OFF)
				{
					/* 10 seconds after end-of-file, stop */
					if (++eof > 10)
						g_RCInput->postMsg((neutrino_msg_t) g_settings.mpkey_stop, 0);
				}
				else
					eof = 0;
			}
#else
				g_RCInput->postMsg((neutrino_msg_t) g_settings.mpkey_stop, 0);
#endif
		}

		if (msg == (neutrino_msg_t) g_settings.mpkey_plugin) {
			//g_PluginList->start_plugin_by_name (g_settings.movieplayer_plugin.c_str (), pidt);
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_stop || (filelist.size() > 0 && msg == (neutrino_msg_t) CRCInput::RC_right)) {
			playstate = CMoviePlayerGui::STOPPED;
			if (msg == (neutrino_msg_t) g_settings.mpkey_stop)
				ShowAbortHintBox();
			playback->RequestAbort();
			if (filelist.size() > 0) {
				if (filelist_it == filelist.end() && repeat_mode == REPEAT_ALL)
					filelist_it = filelist.begin();
				else if (repeat_mode == REPEAT_TRACK)
					--filelist_it;

				if (filelist_it == filelist.end())
					repeat_mode = REPEAT_OFF;
				else {
					file_name = (*filelist_it).Name;
					std::string::size_type pos = file_name.find_last_of('/');
					if(pos != std::string::npos) {
						pretty_name = file_name.substr(pos+1);
						std::replace(pretty_name.begin(), pretty_name.end(), '_', ' ');
					} else
						pretty_name = file_name;
				}
			}
		} else if (msg == (neutrino_msg_t) CRCInput::RC_home) {
			playstate = CMoviePlayerGui::STOPPED;
			ShowAbortHintBox();
			playback->RequestAbort();
			filelist.clear();
			repeat_mode = REPEAT_OFF;
#if HAVE_SPARK_HARDWARE
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_next3dmode) {
			frameBuffer->set3DMode((CFrameBuffer::Mode3D)(((frameBuffer->get3DMode()) + 1) % CFrameBuffer::Mode3D_SIZE));
#endif
		} else if(filelist.size() > 1 && msg == (neutrino_msg_t) CRCInput::RC_left) {
			if (filelist_it != filelist.begin())
				--filelist_it;
			if (filelist_it == filelist.begin())
				filelist_it = filelist.end();
			--filelist_it;
			playstate = CMoviePlayerGui::STOPPED;
			ShowAbortHintBox();
			playback->RequestAbort();
		} else if(timeshift == TSHIFT_MODE_OFF && !isWebTV && !isYT && !isNK && (msg == (neutrino_msg_t) g_settings.mpkey_next_repeat_mode)) {
			repeat_mode = (repeat_mode_enum)((int)repeat_mode + 1);
			if (repeat_mode > (int) REPEAT_ALL)
				repeat_mode = REPEAT_OFF;
			callInfoViewer();
			
		} else if (msg == (neutrino_msg_t) g_settings.key_next43mode) {
			g_videoSettings->next43Mode();
		} else if (msg == (neutrino_msg_t) g_settings.key_switchformat) {
			g_videoSettings->SwitchFormat();
		} else if (msg == (neutrino_msg_t) CRCInput::RC_setup) {
			StopSubtitles(true);
			CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::SHOW_MAINSETTINGS, 0);
			StartSubtitles(true);
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_play) {
			if (playstate > CMoviePlayerGui::PLAY) {
				playstate = CMoviePlayerGui::PLAY;
				speed = 1;
				playback->SetSpeed(speed);
				if (time_forced) {
					FileTime.kill(time_forced);
					time_forced = false;
				}
				//update_lcd = true;
				updateLcd();
			}
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_pause) {
			if (playstate == CMoviePlayerGui::PAUSE) {
				playstate = CMoviePlayerGui::PLAY;
				//CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, false);
				speed = 1;
				playback->SetSpeed(speed);
			} else {
				playstate = CMoviePlayerGui::PAUSE;
				//CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, true);
				speed = 0;
				playback->SetSpeed(speed);
				if (timeshift == TSHIFT_MODE_OFF)
					callInfoViewer(/*duration, position*/);
			}
			//update_lcd = true;
			updateLcd();

		} else if (msg == (neutrino_msg_t) g_settings.mpkey_bookmark) {
			handleMovieBrowser((neutrino_msg_t) g_settings.mpkey_bookmark, position);
			update_lcd = true;
#if 0
			clearSubtitle();
#endif
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_audio) {
			selectAudioPid();
			update_lcd = true;
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_subtitle) {
			selectAudioPid();
#if 0
			selectSubtitle();
			clearSubtitle();
#endif
			update_lcd = true;
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_time) {
			FileTime.switchMode(position, duration);
		} else if ((msg == (neutrino_msg_t) g_settings.mpkey_rewind) ||
			   (msg == (neutrino_msg_t) g_settings.mpkey_forward)) {

			int newspeed;
			if (msg == (neutrino_msg_t) g_settings.mpkey_rewind) {
				newspeed = (speed >= 0) ? -1 : (speed - 1);
			} else {
				newspeed = (speed <= 0) ? 2 : (speed + 1);
			}
			/* if paused, playback->SetSpeed() start slow motion */
			if (playback->SetSpeed(newspeed)) {
				printf("SetSpeed: update speed\n");
				speed = newspeed;
				if (playstate != CMoviePlayerGui::PAUSE)
					playstate = msg == (neutrino_msg_t) g_settings.mpkey_rewind ? CMoviePlayerGui::REW : CMoviePlayerGui::FF;
				updateLcd();
			}
			//update_lcd = true;

			if (!FileTime.IsVisible()) {
				FileTime.show(position, true);
				time_forced = true;
			}

			if (timeshift == TSHIFT_MODE_OFF)
				callInfoViewer(/*duration, position*/);
			else if (time_forced)
				FileTime.show(position, true);

		} else if (msg == CRCInput::RC_1) {	// Jump Backward 1 minute
			SetPosition(-60 * 1000);
		} else if (msg == CRCInput::RC_3) {	// Jump Forward 1 minute
			SetPosition(60 * 1000);
		} else if (msg == CRCInput::RC_4) {	// Jump Backward 5 minutes
			SetPosition(-5 * 60 * 1000);
		} else if (msg == CRCInput::RC_6) {	// Jump Forward 5 minutes
			SetPosition(5 * 60 * 1000);
		} else if (msg == CRCInput::RC_7) {	// Jump Backward 10 minutes
			SetPosition(-10 * 60 * 1000);
		} else if (msg == CRCInput::RC_9) {	// Jump Forward 10 minutes
			SetPosition(10 * 60 * 1000);
		} else if (msg == CRCInput::RC_2) {	// goto start
			SetPosition(0, true);
		} else if (msg == CRCInput::RC_5) {	// goto middle
			SetPosition(duration/2, true);
		} else if (msg == CRCInput::RC_8) {	// goto end
			SetPosition(duration - 60 * 1000, true);
		} else if (msg == CRCInput::RC_page_up) {
			SetPosition(10 * 1000);
		} else if (msg == CRCInput::RC_page_down) {
			SetPosition(-10 * 1000);
		} else if (msg == CRCInput::RC_0) {	// cancel bookmark jump
			handleMovieBrowser(CRCInput::RC_0, position);
		} else if (msg == (neutrino_msg_t) g_settings.mpkey_goto) {
			bool cancel = true;
			playback->GetPosition(position, duration);
			int ss = position/1000;
			int hh = ss/3600;
			ss -= hh * 3600;
			int mm = ss/60;
			ss -= mm * 60;
			std::string Value = to_string(hh/10) + to_string(hh%10) + ":" + to_string(mm/10) + to_string(mm%10) + ":" + to_string(ss/10) + to_string(ss%10);
			CTimeInput jumpTime (LOCALE_MPKEY_GOTO, &Value, NONEXISTANT_LOCALE, NONEXISTANT_LOCALE, NULL, &cancel);
			jumpTime.exec(NULL, "");
			jumpTime.hide();
			if (!cancel && (3 == sscanf(Value.c_str(), "%d:%d:%d", &hh, &mm, &ss)))
				SetPosition(1000 * (hh * 3600 + mm * 60 + ss), true);
		} else if (msg == CRCInput::RC_help || msg == CRCInput::RC_info) {
			callInfoViewer(/*duration, position*/);
			update_lcd = true;
#if 0
			clearSubtitle();
#endif
			//showHelpTS();
#if HAVE_SPARK_HARDWARE
		} else if ((msg == CRCInput::RC_text || msg == (neutrino_msg_t) g_settings.mpkey_vtxt)) {
			int pid = playback->GetFirstTeletextPid();
			if (pid > -1) {
				playback->SetTeletextPid(0);
				StopSubtitles(true);
				if (g_settings.cacheTXT)
					tuxtxt_stop();
				playback->SetTeletextPid(pid);
				tuxtx_stop_subtitle();
				tuxtx_main(pid, 0, 2, true);
				tuxtxt_stop();
				playback->SetTeletextPid(0);
				if (currentttxsub != "") {
					CSubtitleChangeExec SubtitleChanger(playback);
					SubtitleChanger.exec(NULL, currentttxsub);
				}
				StartSubtitles(true);
				frameBuffer->paintBackground();
				//purge input queue
				do
					g_RCInput->getMsg(&msg, &data, 1);
				while (msg != CRCInput::RC_timeout);
			} else if (g_RemoteControl->current_PIDs.PIDs.vtxtpid) {
				StopSubtitles(true);
				// The playback stream doesn't come with teletext.
				tuxtx_main(g_RemoteControl->current_PIDs.PIDs.vtxtpid, 0, 2);
				frameBuffer->paintBackground();
				StartSubtitles(true);
				//purge input queue
				do
					g_RCInput->getMsg(&msg, &data, 1);
				while (msg != CRCInput::RC_timeout);
			}
#endif
		} else if (timeshift != TSHIFT_MODE_OFF && (msg == CRCInput::RC_epg || msg == NeutrinoMessages::SHOW_EPG)) {
			bool restore = FileTime.IsVisible();
			FileTime.kill(time_forced);

			StopSubtitles(true);
			if (msg == CRCInput::RC_epg)
				g_EventList->exec(CNeutrinoApp::getInstance()->channelList->getActiveChannel_ChannelID(), CNeutrinoApp::getInstance()->channelList->getActiveChannelName());
			else if (msg == NeutrinoMessages::SHOW_EPG)
				g_EpgData->show(CNeutrinoApp::getInstance()->channelList->getActiveChannel_ChannelID());
			StartSubtitles(true);
			if (restore)
				FileTime.show(position);
		} else if (msg == NeutrinoMessages::SHOW_EPG) {
			handleMovieBrowser(NeutrinoMessages::SHOW_EPG, position);
		} else if (msg == (neutrino_msg_t) g_settings.key_screenshot) {

			char ending[(sizeof(int)*2) + 6] = ".png";
			if(!g_settings.screenshot_cover)
				snprintf(ending, sizeof(ending) - 1, "_%x.png", position);

			std::string fname = file_name;
			std::string::size_type pos = fname.find_last_of('.');
			if(pos != std::string::npos) {
				fname.replace(pos, fname.length(), ending);
			} else
				fname += ending;

			if(!g_settings.screenshot_cover){
				pos = fname.find_last_of('/');
				if(pos != std::string::npos) {
					fname.replace(0, pos, g_settings.screenshot_dir);
				}
			}

#if 0 // TODO disable overwrite ?
			if(!access(fname, F_OK)) {
			}
#endif
			if (g_settings.screenshot_cover) {
				unlink(fname.c_str());
				CVFD::getInstance()->ShowText("SCREENSHOT");
				CHintBox hintbox(LOCALE_SCREENSHOT_MENU, g_Locale->getText(LOCALE_SCREENSHOT_PLEASE_WAIT_COVER), 450, NEUTRINO_ICON_MOVIEPLAYER);
				hintbox.paint();
				CScreenShot sc(fname, CScreenShot::FORMAT_PNG);
				sc.EnableVideo(true);
				sc.EnableOSD(false);
				sc.Start();
				hintbox.hide();
			} else {
				CVFD::getInstance()->ShowText("SCREENSHOT");
				CHintBox *hintbox = NULL;
				if (g_settings.screenshot_mode == 1) {
					hintbox = new CHintBox(LOCALE_SCREENSHOT_MENU, g_Locale->getText(LOCALE_SCREENSHOT_PLEASE_WAIT), 450, NEUTRINO_ICON_MOVIEPLAYER);
					hintbox->paint();
				}
				CScreenShot sc(fname, CScreenShot::FORMAT_PNG);
				sc.Start();
				if (hintbox)
					hintbox->hide();
			}

		} else if (msg == NeutrinoMessages::EVT_SUBT_MESSAGE) {
#if 0
			showSubtitle(data);
#endif
		} else if (msg == NeutrinoMessages::ANNOUNCE_RECORD ||
				msg == NeutrinoMessages::RECORD_START) {
			CNeutrinoApp::getInstance()->handleMsg(msg, data);
		} else if (msg == NeutrinoMessages::ZAPTO ||
				msg == NeutrinoMessages::STANDBY_ON ||
				msg == NeutrinoMessages::SHUTDOWN ||
				((msg == NeutrinoMessages::SLEEPTIMER) && !data) ) {	// Exit for Record/Zapto Timers
			printf("CMoviePlayerGui::PlayFile: ZAPTO etc..\n");
			if (msg != NeutrinoMessages::ZAPTO)
				menu_ret = menu_return::RETURN_EXIT_ALL;

			playstate = CMoviePlayerGui::STOPPED;
			g_RCInput->postMsg(msg, data);
		} else if (msg == CRCInput::RC_timeout) {
			// nothing
		} else if (CNeutrinoApp::getInstance()->usermenu.showUserMenu(msg)) {
		} else if (msg == CRCInput::RC_sat || msg == CRCInput::RC_favorites) {
			//FIXME do nothing ?
#if 0
		} else if ((msg == CRCInput::RC_record) && !isWebTV && !isYT && !isNK) {
			std::string shot = file_name;
			size_t found = shot.rfind(".ts");
			if (found == (shot.length() - 3)) {
				shot.erase(found);
				shot.append(".png");
				my_system(4, "/bin/grab", "-vbr", "360", shot.c_str());
			}
#endif
		} else {
			if (CNeutrinoApp::getInstance()->handleMsg(msg, data) & messages_return::cancel_all) {
				printf("CMoviePlayerGui::PlayFile: neutrino handleMsg messages_return::cancel_all\n");
				playstate = CMoviePlayerGui::STOPPED;
				menu_ret = menu_return::RETURN_EXIT_ALL;
				repeat_mode = REPEAT_OFF;
			}
			else if (msg <= CRCInput::RC_MaxRC) {
				update_lcd = true;
#if 0
				clearSubtitle();
#endif
			}
		}

		if (playstate == CMoviePlayerGui::STOPPED) {
			printf("CMoviePlayerGui::PlayFile: exit, isMovieBrowser %d p_movie_info %p\n", isMovieBrowser, p_movie_info);
			playstate = CMoviePlayerGui::STOPPED;
			handleMovieBrowser((neutrino_msg_t) g_settings.mpkey_stop, position);
		}
	}
}

void CMoviePlayerGui::PlayFileEnd(bool restore)
{
	if (!stopped) {
		CSubtitleChangeExec SubtitleChanger(playback);
		SubtitleChanger.exec(NULL, "off");
		playback->SetSpeed(1);
		playback->Close();
	}

#if HAVE_SPARK_HARDWARE
	frameBuffer->set3DMode(old3dmode);
	CScreenSetup cSS;
	cSS.showBorder(CZapit::getInstance()->GetCurrentChannelID());
#endif
#ifdef ENABLE_GRAPHLCD
	if (p_movie_info)
		nGLCD::unlockChannel();
#endif
	if (iso_file) {
		iso_file = false;
		if (umount2(ISO_MOUNT_POINT, MNT_FORCE))
			perror(ISO_MOUNT_POINT);
	}

	FileTime.kill(time_forced);
	CVFD::getInstance()->ShowIcon(FP_ICON_PLAY, false);
	CVFD::getInstance()->ShowIcon(FP_ICON_PAUSE, false);

	HideHintBox();

	if (restore)
		restoreNeutrino();

	CAudioMute::getInstance()->enableMuteIcon(false);
	InfoClock->enableInfoClock(false);
	stopped = true;
}

void CMoviePlayerGui::callInfoViewer(/*const int duration, const int curr_pos*/)
{
	if (timeshift != TSHIFT_MODE_OFF) {
		g_InfoViewer->showTitle(CNeutrinoApp::getInstance()->channelList->getActiveChannelNumber(),
				CNeutrinoApp::getInstance()->channelList->getActiveChannelName(),
				CNeutrinoApp::getInstance()->channelList->getActiveSatellitePosition(),
				CNeutrinoApp::getInstance()->channelList->getActiveChannel_ChannelID());
		return;
	}
	currentaudioname = "Unknown";
	getCurrentAudioName(currentaudioname);

	if (p_movie_info) {
		std::string channelName = p_movie_info->epgChannel;
		if (channelName.empty()) {
			if (isYT)
				channelName = g_Locale->getText(LOCALE_MOVIEPLAYER_YTPLAYBACK);
			else if (isNK)
				channelName = g_Locale->getText(LOCALE_MOVIEPLAYER_NKPLAYBACK);
			else if (isWebTV)
				channelName = g_Locale->getText(LOCALE_WEBTV_HEAD);
			else
				channelName = g_Locale->getText(LOCALE_MOVIEPLAYER_FILEPLAYBACK);
		}
		g_InfoViewer->showMovieTitle(playstate, GET_CHANNEL_ID_FROM_EVENT_ID(p_movie_info->epgEpgId),
					     channelName, p_movie_info->epgTitle, p_movie_info->epgInfo1,
					     duration, position, repeat_mode);
		return;
	}

	/* fallthrough: use the filename as title */
	g_InfoViewer->showMovieTitle(playstate, 0, pretty_name, "", "", duration, position, repeat_mode);
}

bool CMoviePlayerGui::getAudioName(int apid, std::string &apidtitle)
{
	if (p_movie_info == NULL)
		return false;

	for (int i = 0; i < (int)p_movie_info->audioPids.size(); i++) {
		if (p_movie_info->audioPids[i].epgAudioPid == apid && !p_movie_info->audioPids[i].epgAudioPidName.empty()) {
			apidtitle = p_movie_info->audioPids[i].epgAudioPidName;
			return true;
		}
	}
	return false;
}

void CMoviePlayerGui::addAudioFormat(int count, std::string &apidtitle, bool& enabled)
{
	enabled = true;
	switch(ac3flags[count])
	{
		case 1: /*AC3*/
			if (apidtitle.find("AC3") == std::string::npos)
				apidtitle.append(" (AC3)");
			break;
		case 2: /*teletext*/
			apidtitle.append(" (Teletext)");
			enabled = false;
			break;
		case 3: /*MP2*/
			apidtitle.append(" (MP2)");
			break;
		case 4: /*MP3*/
			apidtitle.append(" (MP3)");
			break;
		case 5: /*AAC*/
			apidtitle.append(" (AAC)");
			break;
		case 6: /*DTS*/
#if !defined (BOXMODEL_APOLLO)
			if (apidtitle.find("DTS") == std::string::npos)
				apidtitle.append(" (DTS)");
			else
#endif
				enabled = false;
			break;
		case 7: /*EAC3*/
			if (apidtitle.find("EAC3") == std::string::npos)
				apidtitle.append(" (EAC3)");
			break;
		default:
			break;
	}
}

void CMoviePlayerGui::getCurrentAudioName(std::string &audioname)
{
	numpida = REC_MAX_APIDS;
	playback->FindAllPids(apids, ac3flags, &numpida, language);
	if (numpida && !currentapid)
		currentapid = apids[0];
	for (unsigned int count = 0; count < numpida; count++)
		if(currentapid == apids[count]){
			if (getAudioName(apids[count], audioname))
				return;
			audioname = language[count];
			return;
		}
}

void CMoviePlayerGui::selectAudioPid()
{
	CAudioSelectMenuHandler APIDSelector;
	StopSubtitles(true);
	APIDSelector.exec(NULL, "-1");
	StartSubtitles(true);
}

void CMoviePlayerGui::handleMovieBrowser(neutrino_msg_t msg, int /*position*/)
{
	CMovieInfo cMovieInfo;	// funktions to save and load movie info

	static int jump_not_until = 0;	// any jump shall be avoided until this time (in seconds from moviestart)
	static MI_BOOKMARK new_bookmark;	// used for new movie info bookmarks created from the movieplayer

	static int width = 280;
	static int height = 65;

	static int x = frameBuffer->getScreenX() + (frameBuffer->getScreenWidth() - width) / 2;
	static int y = frameBuffer->getScreenY() + frameBuffer->getScreenHeight() - height - 20;

	static CBox boxposition(x, y, width, height);	// window position for the hint boxes

	static CTextBox endHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_MOVIEEND), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);
	static CTextBox comHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_JUMPFORWARD), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);
	static CTextBox loopHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_JUMPBACKWARD), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);
	static CTextBox newLoopHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_NEWBOOK_BACKWARD), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);
	static CTextBox newComHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_NEWBOOK_FORWARD), NULL, CTextBox::CENTER /*CTextBox::AUTO_WIDTH | CTextBox::AUTO_HIGH */ , &boxposition);

	static bool showEndHintBox = false;	// flag to check whether the box shall be painted
	static bool showComHintBox = false;	// flag to check whether the box shall be painted
	static bool showLoopHintBox = false;	// flag to check whether the box shall be painted

	int play_sec = position / 1000;	// get current seconds from moviestart
	if (msg == CRCInput::RC_nokey) {
		printf("CMoviePlayerGui::handleMovieBrowser: reset vars\n");
		// reset statics
		jump_not_until = 0;
		showEndHintBox = showComHintBox = showLoopHintBox = false;
		new_bookmark.pos = 0;
		// move in case osd position changed
		int newx = frameBuffer->getScreenX() + (frameBuffer->getScreenWidth() - width) / 2;
		int newy = frameBuffer->getScreenY() + frameBuffer->getScreenHeight() - height - 20;
		endHintBox.movePosition(newx, newy);
		comHintBox.movePosition(newx, newy);
		loopHintBox.movePosition(newx, newy);
		newLoopHintBox.movePosition(newx, newy);
		newComHintBox.movePosition(newx, newy);
		return;
	}
	else if (msg == (neutrino_msg_t) g_settings.mpkey_stop) {
		// if we have a movie information, try to save the stop position
		printf("CMoviePlayerGui::handleMovieBrowser: stop, isMovieBrowser %d p_movie_info %p\n", isMovieBrowser, p_movie_info);
		if (isMovieBrowser && p_movie_info) {
			timeb current_time;
			ftime(&current_time);
			p_movie_info->dateOfLastPlay = current_time.time;
			current_time.time = time(NULL);
			p_movie_info->bookmarks.lastPlayStop = position / 1000;
			if (!isWebTV && !isYT && !isNK)
				cMovieInfo.saveMovieInfo(*p_movie_info);
			//p_movie_info->fileInfoStale(); //TODO: we might to tell the Moviebrowser that the movie info has changed, but this could cause long reload times  when reentering the Moviebrowser
		}
	}
	else if ((msg == 0) && isMovieBrowser && (playstate == CMoviePlayerGui::PLAY) && p_movie_info) {
		if (play_sec + 10 < jump_not_until || play_sec > jump_not_until + 10)
			jump_not_until = 0;	// check if !jump is stale (e.g. if user jumped forward or backward)

		// do bookmark activities only, if there is no new bookmark started
		if (new_bookmark.pos != 0)
			return;
#ifdef DEBUG
		//printf("CMoviePlayerGui::handleMovieBrowser: process bookmarks\n");
#endif
		if (p_movie_info->bookmarks.end != 0) {
			// *********** Check for stop position *******************************
			if (play_sec >= p_movie_info->bookmarks.end - MOVIE_HINT_BOX_TIMER && play_sec < p_movie_info->bookmarks.end && play_sec > jump_not_until) {
				if (showEndHintBox == false) {
					endHintBox.paint();	// we are 5 sec before the end postition, show warning
					showEndHintBox = true;
					TRACE("[mp]  user stop in 5 sec...\r\n");
				}
			} else {
				if (showEndHintBox == true) {
					endHintBox.hide();	// if we showed the warning before, hide the box again
					showEndHintBox = false;
				}
			}

			if (play_sec >= p_movie_info->bookmarks.end && play_sec <= p_movie_info->bookmarks.end + 2 && play_sec > jump_not_until)	// stop playing
			{
				// *********** we ARE close behind the stop position, stop playing *******************************
				TRACE("[mp]  user stop: play_sec %d bookmarks.end %d jump_not_until %d\n", play_sec, p_movie_info->bookmarks.end, jump_not_until);
				playstate = CMoviePlayerGui::STOPPED;
				return;
			}
		}
		// *************  Check for bookmark jumps *******************************
		showLoopHintBox = false;
		showComHintBox = false;
		for (int book_nr = 0; book_nr < MI_MOVIE_BOOK_USER_MAX; book_nr++) {
			if (p_movie_info->bookmarks.user[book_nr].pos != 0 && p_movie_info->bookmarks.user[book_nr].length != 0) {
				// valid bookmark found, now check if we are close before or after it
				if (play_sec >= p_movie_info->bookmarks.user[book_nr].pos - MOVIE_HINT_BOX_TIMER && play_sec < p_movie_info->bookmarks.user[book_nr].pos && play_sec > jump_not_until) {
					if (p_movie_info->bookmarks.user[book_nr].length < 0)
						showLoopHintBox = true;	// we are 5 sec before , show warning
					else if (p_movie_info->bookmarks.user[book_nr].length > 0)
						showComHintBox = true;	// we are 5 sec before, show warning
					//else  // TODO should we show a plain bookmark infomation as well?
				}

				if (play_sec >= p_movie_info->bookmarks.user[book_nr].pos && play_sec <= p_movie_info->bookmarks.user[book_nr].pos + 2 && play_sec > jump_not_until)	//
				{
					//for plain bookmark, the following calc shall result in 0 (no jump)
					int jumpseconds = p_movie_info->bookmarks.user[book_nr].length;

					// we are close behind the bookmark, do bookmark activity (if any)
					if (p_movie_info->bookmarks.user[book_nr].length < 0) {
						// if the jump back time is to less, it does sometimes cause problems (it does probably jump only 5 sec which will cause the next jump, and so on)
						if (jumpseconds > -15)
							jumpseconds = -15;

						SetPosition(jumpseconds * 1000);
					} else if (p_movie_info->bookmarks.user[book_nr].length > 0) {
						// jump at least 15 seconds
						if (jumpseconds < 15)
							jumpseconds = 15;

						SetPosition(jumpseconds * 1000);
					}
					TRACE("[mp]  do jump %d sec\r\n", jumpseconds);
					break;	// do no further bookmark checks
				}
			}
		}
		// check if we shall show the commercial warning
		if (showComHintBox == true) {
			comHintBox.paint();
			TRACE("[mp]  com jump in 5 sec...\r\n");
		} else
			comHintBox.hide();

		// check if we shall show the loop warning
		if (showLoopHintBox == true) {
			loopHintBox.paint();
			TRACE("[mp]  loop jump in 5 sec...\r\n");
		} else
			loopHintBox.hide();

		return;
	} else if (msg == CRCInput::RC_0) {	// cancel bookmark jump
		printf("CMoviePlayerGui::handleMovieBrowser: CRCInput::RC_0\n");
		if (isMovieBrowser == true) {
			if (new_bookmark.pos != 0) {
				new_bookmark.pos = 0;	// stop current bookmark activity, TODO:  might bemoved to another key
				newLoopHintBox.hide();	// hide hint box if any
				newComHintBox.hide();
			}
			comHintBox.hide();
			loopHintBox.hide();
			jump_not_until = (position / 1000) + 10; // avoid bookmark jumping for the next 10 seconds, , TODO:  might be moved to another key
		}
		return;
	}
	else if (msg == (neutrino_msg_t) g_settings.mpkey_bookmark) {
		if (newComHintBox.isPainted()) {
			// yes, let's get the end pos of the jump forward
			new_bookmark.length = play_sec - new_bookmark.pos;
			TRACE("[mp] commercial length: %d\r\n", new_bookmark.length);
			if (cMovieInfo.addNewBookmark(p_movie_info, new_bookmark) == true) {
				cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
			}
			new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
			newComHintBox.hide();
		} else if (newLoopHintBox.isPainted()) {
			// yes, let's get the end pos of the jump backward
			new_bookmark.length = new_bookmark.pos - play_sec;
			new_bookmark.pos = play_sec;
			TRACE("[mp] loop length: %d\r\n", new_bookmark.length);
			if (cMovieInfo.addNewBookmark(p_movie_info, new_bookmark) == true) {
				cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
				jump_not_until = play_sec + 5;	// avoid jumping for this time
			}
			new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
			newLoopHintBox.hide();
		} else {
			std::vector<int> positions; std::vector<std::string> titles;
			playback->GetChapters(positions, titles);
			if (positions.empty() && (isWebTV || isYT || isNK))
				return;

			CMenuWidget bookStartMenu(positions.empty() ? LOCALE_MOVIEBROWSER_BOOK_ADD : LOCALE_MOVIEBROWSER_MENU_MAIN_BOOKMARKS, NEUTRINO_ICON_AUDIO);
			bookStartMenu.addIntroItems();
#if 0 // not supported, TODO
			bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEPLAYER_HEAD, !isMovieBrowser, NULL, &cSelectedMenuBookStart[0]));
			bookStartMenu.addItem(GenericMenuSeparatorLine);
#endif
			int chapter_item_offset = -1;
			std::string positions_str[positions.size() + 1];
			if (!positions.empty()) {
				chapter_item_offset = bookStartMenu.getItemsCount();
				for (unsigned i = 0; i < positions.size(); i++) {
					titles[i] = isUTF8(titles[i]) ? titles[i]: convertLatin1UTF8(titles[i]);
					time_t sec = positions[i]/1000;
					char val[10];
					strftime(val, sizeof(val), "%H:%M:%S", gmtime(&sec));
					positions_str[i] = val;
					bookStartMenu.addItem(new CMenuForwarder(titles[i].c_str(), true, positions_str[i], NULL, NULL, CRCInput::convertDigitToKey(i + 1)));
				}
			}

			int bookmark_item_offset = -1;
			if (isMovieBrowser) {
				if (!positions.empty())
					bookStartMenu.addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_MOVIEBROWSER_BOOK_ADD));
				bookmark_item_offset = bookStartMenu.getItemsCount();
				bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_NEW));
				bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_TYPE_FORWARD));
				bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_TYPE_BACKWARD));
				bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_MOVIESTART));
				bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_MOVIEEND));
			}

			// no, nothing else to do, we open a new bookmark menu
			new_bookmark.name = "";	// use default name
			new_bookmark.pos = 0;
			new_bookmark.length = 0;

			// next seems return menu_return::RETURN_EXIT, if something selected
			int selected = (bookStartMenu.exec(NULL, "none") != menu_return::RETURN_EXIT) ? bookStartMenu.getSelected() : -1;
#if 0 // not supported, TODO
			if (cSelectedMenuBookStart[0].selected == true) {
				/* Movieplayer bookmark */
				if (bookmarkmanager->getBookmarkCount() < bookmarkmanager->getMaxBookmarkCount()) {
					char timerstring[200];
					sprintf(timerstring, "%lld", play_sec);
					std::string bookmarktime = timerstring;
					fprintf(stderr, "fileposition: %lld timerstring: %s bookmarktime: %s\n", play_sec, timerstring, bookmarktime.c_str());
					bookmarkmanager->createBookmark(file_name, bookmarktime);
				} else {
					fprintf(stderr, "too many bookmarks\n");
					DisplayErrorMessage(g_Locale->getText(LOCALE_MOVIEPLAYER_TOOMANYBOOKMARKS));	// UTF-8
				}
				cSelectedMenuBookStart[0].selected = false;	// clear for next bookmark menu
			} else
#endif
			int chapter_item_selected = (selected > -1 && chapter_item_offset > -1) ? selected - chapter_item_offset : -1;
			if (chapter_item_selected < 0 || chapter_item_selected >= (int)positions.size())
				chapter_item_selected = -1;

			int bookmark_item_selected = (selected > -1 && bookmark_item_offset > -1) ? selected - bookmark_item_offset : -1;
			if (bookmark_item_selected < 0 || bookmark_item_selected > 4)
				bookmark_item_selected = -1;

			if (chapter_item_selected > -1) {
				playback->SetPosition(positions[chapter_item_selected], true);
			} else if (bookmark_item_selected > -1) {
				playback->GetPosition(position, duration);
				play_sec = position / 1000;
				switch (bookmark_item_selected) {
					case 0:
						/* Moviebrowser plain bookmark */
						new_bookmark.pos = play_sec;
						new_bookmark.length = 0;
						if (cMovieInfo.addNewBookmark(p_movie_info, new_bookmark))
							cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
						new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
						break;
					case 1:
						/* Moviebrowser jump forward bookmark */
						new_bookmark.pos = play_sec;
						TRACE("[mp] new bookmark 1. pos: %d\r\n", new_bookmark.pos);
						newComHintBox.paint();
						break;
					case 2:
						/* Moviebrowser jump backward bookmark */
						new_bookmark.pos = play_sec;
						TRACE("[mp] new bookmark 1. pos: %d\r\n", new_bookmark.pos);
						newLoopHintBox.paint();
						break;
					case 3:
						/* Moviebrowser movie start bookmark */
						p_movie_info->bookmarks.start = play_sec;
						TRACE("[mp] New movie start pos: %d\r\n", p_movie_info->bookmarks.start);
						cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
						break;
					case 4:
						/* Moviebrowser movie end bookmark */
						p_movie_info->bookmarks.end = play_sec;
						TRACE("[mp]  New movie end pos: %d\r\n", p_movie_info->bookmarks.start);
						cMovieInfo.saveMovieInfo(*p_movie_info);	/* save immediately in xml file */
						break;
				}
			}
		}
	} else if (msg == NeutrinoMessages::SHOW_EPG && p_movie_info) {
		CTimeOSD::mode m_mode = FileTime.getMode();
		bool restore = FileTime.IsVisible();
		if (restore)
			FileTime.kill();
		InfoClock->enableInfoClock(false);

		cMovieInfo.showMovieInfo(*p_movie_info);

		InfoClock->enableInfoClock(true);
		if (restore) {
			FileTime.setMode(m_mode);
			FileTime.update(position, duration);
		}
	}
}

void CMoviePlayerGui::UpdatePosition()
{
	if (playback->GetPosition(position, duration)) {
		if (duration > 100)
			file_prozent = (unsigned char) (position / (duration / 100));
		FileTime.update(position, duration);
#ifdef DEBUG
		printf("CMoviePlayerGui::PlayFile: speed %d position %d duration %d (%d, %d%%)\n", speed, position, duration, duration-position, file_prozent);
#endif
	}
}

void CMoviePlayerGui::showHelpTS()
{
	Helpbox helpbox;
	helpbox.addLine(NEUTRINO_ICON_BUTTON_RED, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP1));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_GREEN, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP2));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_YELLOW, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP3));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_BLUE, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP4));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_MENU, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP5));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_1, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP6));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_3, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP7));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_4, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP8));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_6, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP9));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_7, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP10));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_9, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP11));
	helpbox.addLine(g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP12));
	helpbox.show(LOCALE_MESSAGEBOX_INFO);
}

void CMoviePlayerGui::StopSubtitles(bool enable_glcd_mirroring __attribute__((unused)))
{
#if HAVE_SPARK_HARDWARE
	printf("[CMoviePlayerGui] %s\n", __FUNCTION__);
	int ttx, ttxpid, ttxpage;

	int current_sub = playback->GetSubtitlePid();
	if (current_sub > -1)
		dvbsub_pause();
	tuxtx_subtitle_running(&ttxpid, &ttxpage, &ttx);
	if (ttx) {
		tuxtx_pause_subtitle(true);
		frameBuffer->paintBackground();
	}
#ifdef ENABLE_GRAPHLCD
	if (enable_glcd_mirroring)
		nGLCD::MirrorOSD(g_settings.glcd_mirror_osd);
#endif
#endif
}

void CMoviePlayerGui::StartSubtitles(bool show __attribute__((unused)))
{
#if HAVE_SPARK_HARDWARE
	printf("[CMoviePlayerGui] %s: %s\n", __FUNCTION__, show ? "Show" : "Not show");
#ifdef ENABLE_GRAPHLCD
	nGLCD::MirrorOSD(false);
#endif

	if(!show)
		return;
	int current_sub = playback->GetSubtitlePid();
	if (current_sub > -1)
		dvbsub_start(current_sub, true);
	tuxtx_pause_subtitle(false);
#endif
}

#if 0
void CMoviePlayerGui::selectChapter()
{
	std::vector<int> positions; std::vector<std::string> titles;
	playback->GetChapters(positions, titles);
	if (positions.empty())
		return;

	CMenuWidget ChSelector(LOCALE_MOVIEBROWSER_MENU_MAIN_BOOKMARKS, NEUTRINO_ICON_AUDIO);
	ChSelector.addIntroItems();

	int select = -1;
	CMenuSelectorTarget * selector = new CMenuSelectorTarget(&select);
	char cnt[5];
	for (unsigned i = 0; i < positions.size(); i++) {
		sprintf(cnt, "%d", i);
		CMenuForwarder * item = new CMenuForwarder(titles[i].c_str(), true, NULL, selector, cnt, CRCInput::convertDigitToKey(i + 1));
		ChSelector.addItem(item, position > positions[i]);
	}
	ChSelector.exec(NULL, "");
	delete selector;
	printf("CMoviePlayerGui::selectChapter: selected %d (%d)\n", select, (select >= 0) ? positions[select] : -1);
	if(select >= 0)
		playback->SetPosition(positions[select], true);
}
#endif

bool CMoviePlayerGui::setAPID(unsigned int i) {
	if (currentapid != apids[i]) {
		currentapid = apids[i];
		currentac3 = ac3flags[i];
		SetStreamType();
		playback->SetAPid(currentapid, currentac3);
	}
	return (i < numpida);
}

std::string CMoviePlayerGui::getAPIDDesc(unsigned int i)
{
	std::string apidtitle;
	if (i < numpida)
		getAudioName(apids[i], apidtitle);
	if (apidtitle == "")
		apidtitle = "Stream " + to_string(i);
	return apidtitle;
}

unsigned int CMoviePlayerGui::getAPID(unsigned int i)
{
	if (i < numpida)
		return apids[i];
	return -1;
}

unsigned int CMoviePlayerGui::getAPID(void)
{
	for (unsigned int i = 0; i < numpida; i++)
		if (apids[i] == currentapid)
			return i;
	return -1;
}

unsigned int CMoviePlayerGui::getAPIDCount(void)
{
	unsigned int count = 0;
	numpida = REC_MAX_APIDS;
	playback->FindAllPids(apids, ac3flags, &numpida, language);
	for (unsigned int i = 0; i < numpida; i++) {
		if (i != count) {
			apids[count] = apids[i];
			ac3flags[count] = ac3flags[i];
			language[count] = language[i];
		}
		if (language[i].empty()) {
			language[i] = "Stream ";
			language[i] += to_string(count);
		}
		bool ena = false;
		addAudioFormat(i, language[i], ena);
		if (ena)
			count++;
	}
	numpida = count;
	return numpida;
}

unsigned int CMoviePlayerGui::getSubtitleCount(void)
{
	// these may change in-stream
	numpids = REC_MAX_SPIDS;
	playback->FindAllSubtitlePids(spids, &numpids, slanguage);
	numpidt = REC_MAX_TPIDS;
	playback->FindAllTeletextsubtitlePids(tpids, &numpidt, tlanguage, tmag, tpage);

	return numpids + numpidt;
}

CZapitAbsSub* CMoviePlayerGui::getChannelSub(unsigned int i, CZapitAbsSub **s)
{
	if (i < numpidt) {
		CZapitTTXSub *_s = new CZapitTTXSub;
		_s->thisSubType = CZapitAbsSub::TTX;
		_s->pId = tpids[i];
		_s->ISO639_language_code = tlanguage[i];
		_s->teletext_magazine_number = tmag[i];
		_s->teletext_page_number = tpage[i];
		*s = _s;
		return *s;
	}
	i -= numpidt;
	if (i < numpids) {
		CZapitAbsSub *_s = new CZapitAbsSub;
		_s->thisSubType = CZapitAbsSub::SUB;
		_s->pId = spids[i];
		_s->ISO639_language_code = slanguage[i];
		*s = _s;
		return *s;
	}
	return NULL;
}

int CMoviePlayerGui::getCurrentSubPid(CZapitAbsSub::ZapitSubtitleType st)
{
	switch(st) {
		case CZapitAbsSub::DVB:
		case CZapitAbsSub::SUB:
			return playback->GetSubtitlePid();
		case CZapitAbsSub::TTX:
			return -1; // FIXME ... caller would need both pid and page
	}
	return -1;
}

t_channel_id CMoviePlayerGui::getChannelId(void)
{
	return p_movie_info ? p_movie_info->epgId : 0;
}

void CMoviePlayerGui::getAPID(int &apid, unsigned int &is_ac3)
{
	apid = currentapid, is_ac3 = (currentac3 == AUDIO_FMT_DOLBY_DIGITAL || currentac3 == AUDIO_FMT_DD_PLUS);
}

bool CMoviePlayerGui::getAPID(unsigned int i, int &apid, unsigned int &is_ac3)
{
	if (i < numpida) {
		apid = apids[i];
		is_ac3 = (ac3flags[i] == 1);
		return true;
	}
	return false;
}

size_t CMoviePlayerGui::GetReadCount()
{
#if HAVE_SPARK_HARDWARE
	uint64_t this_read = 0;
	this_read = playback->GetReadCount();
	uint64_t res;
	if (this_read < last_read)
		res = 0;
	else
		res = this_read - last_read;
	last_read = this_read;
	return (size_t) res;
#else
	return 0;
#endif
}

void CMoviePlayerGui::Pause(bool b)
{
	if (b && (playstate == CMoviePlayerGui::PAUSE))
		b = !b;
	if (b) {
		playback->SetSpeed(0);
		playstate = CMoviePlayerGui::PAUSE;
	} else {
		playback->SetSpeed(1);
		playstate = CMoviePlayerGui::PLAY;
	}
}

void CMoviePlayerGui::SetStreamType(void)
{
	switch(currentac3)
	{
		case 0:
		case 3:
			StreamType = AUDIO_FMT_MPEG;
			break;
		case 1: /*AC3*/
			StreamType = AUDIO_FMT_DOLBY_DIGITAL;
			break;
		case 4: /*MP3*/
			StreamType = AUDIO_FMT_MP3;
			break;
		case 5: /*AAC*/
			StreamType = AUDIO_FMT_AAC;
			break;
		case 6: /*DTS*/
			StreamType = AUDIO_FMT_DTS;
			break;
		case 7: /*EAC3*/
			StreamType = AUDIO_FMT_DD_PLUS;
			break;
		default:
			StreamType = AUDIO_FMT_AUTO;
			break;
	}
}

void CMoviePlayerGui::selectAutoLang()
{
	if (g_settings.auto_lang &&  (numpida > 1)) {
		int pref_idx = -1;

		playback->FindAllPids(apids, ac3flags, &numpida, language);
		for (int i = 0; i < 3; i++) {
			for (unsigned j = 0; j < numpida; j++) {
				std::map<std::string, std::string>::const_iterator it;
				for (it = iso639.begin(); it != iso639.end(); ++it) {
					if (g_settings.pref_lang[i] == it->second && strncasecmp(language[j].c_str(), it->first.c_str(), 3) == 0) {
						pref_idx = j;
						break;
					}
				}
				if (pref_idx >= 0)
					break;
			}
			if (pref_idx >= 0)
				break;
		}
		if (pref_idx >= 0) {
			currentapid = apids[pref_idx];
			currentac3 = ac3flags[pref_idx];
			playback->SetAPid(currentapid, currentac3);
		}
	}
}

void CMoviePlayerGui::parsePlaylist(CFile *file)
{
	std::ifstream infile;
	char cLine[1024];
	char name[1024] = { 0 };
	infile.open(file->Name.c_str(), std::ifstream::in);
	while (infile.good())
	{
		infile.getline(cLine, sizeof(cLine));
		if (cLine[strlen(cLine)-1]=='\r')
			cLine[strlen(cLine)-1]=0;

		int dur;
		sscanf(cLine, "#EXTINF:%d,%[^\n]\n", &dur, name);
		if (strlen(cLine) > 0 && cLine[0]!='#')
		{
			char *url = strstr(cLine, "://");
			if (url) {
				while (url > cLine && isalpha(*(url - 1)))
					url--;
				printf("name %s [%d] url: %s\n", name, dur, url);
				file_name = url;
				if (strlen(name))
					pretty_name = name;
			}
		}
	}
}

bool CMoviePlayerGui::mountIso(CFile *file)
{
	printf("ISO file passed: %s\n", file->Name.c_str());
	safe_mkdir(ISO_MOUNT_POINT);
	if (my_system(5, "mount", "-o", "loop", file->Name.c_str(), ISO_MOUNT_POINT) == 0) {
		makeFilename();
		file_name = "bluray:" ISO_MOUNT_POINT;
		iso_file = true;
		return true;
	}
	return false;
}
