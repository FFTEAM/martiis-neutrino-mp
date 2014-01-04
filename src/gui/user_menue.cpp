/*

	user_menue setup implementation - Neutrino-GUI
	based up implementation by Günther

	Copyright (C) 2001 Steffen Hehn 'McClean'
	and some other guys
	Homepage: http://dbox.cyberphoria.org/

	Rework
	Copyright (C) 2011 T. Graf 'dbt'
	Homepage: http://www.dbox2-tuning.net/


        License: GPL

        This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Library General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; if not, write to the
	Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
	Boston, MA  02110-1301, USA.

		
	NOTE for ignorant distributors:
	It's not allowed to distribute any compiled parts of this code, if you don't accept the terms of GPL.
	Please read it and understand it right!
	This means for you: Hold it, if not, leave it! You could face legal action! 
	Otherwise ask the copyright owners, anything else would be theft!
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "user_menue.h"
#include "user_menue_setup.h"
#include "subchannel_select.h"
#include "favorites.h"
#include "adzap.h"
#include "3dsetup.h"
#include "audio_select.h"
#include "streaminfo2.h"
#include "epgplus.h"
#include "epgview.h"
#include "eventlist.h"
#include "movieplayer.h"
#include "timerlist.h"
#include "plugins.h"
#include "imageinfo.h"
#include "dboxinfo.h"
#if !HAVE_SPARK_HARDWARE
#include "cam_menu.h"
#endif
#include "pluginlist.h"
#include "infoclock.h"

#include <global.h>
#include <neutrino.h>
#include <mymenu.h>

#include <gui/widget/icons.h>

#include <driver/radiotext.h>
#include <driver/record.h>
#include <driver/screen_max.h>

#include <daemonc/remotecontrol.h>
extern CRemoteControl * g_RemoteControl; /* neutrino.cpp */
// extern CPlugins * g_PluginList;
#if !HAVE_SPARK_HARDWARE
extern CCAMMenuHandler * g_CamHandler;
#endif
// 
#include <system/debug.h>

CUserMenu::CUserMenu()
{
	width = 0;
}

CUserMenu::~CUserMenu()
{
	
}

// USERMENU
bool CUserMenu::showUserMenu(neutrino_msg_t msg)
{
	int button = -1;
	for (int i = 0; i < SNeutrinoSettings::BUTTON_MAX; i++)
		if (g_settings.usermenu_key[i] == (int) msg) {
			button = i;
			break;
		}

	if (button < 0)
		return false;

	int pers = -1;
	switch(msg) {
		case CRCInput::RC_red:
			pers = SNeutrinoSettings::P_MAIN_RED_BUTTON;
			button = SNeutrinoSettings::BUTTON_RED;
			break;
		case CRCInput::RC_green:
			pers = SNeutrinoSettings::P_MAIN_GREEN_BUTTON;
			button = SNeutrinoSettings::BUTTON_GREEN;
			break;
		case CRCInput::RC_yellow:
			pers = SNeutrinoSettings::P_MAIN_YELLOW_BUTTON;
			button = SNeutrinoSettings::BUTTON_YELLOW;
			break;
		case CRCInput::RC_blue:
			pers = SNeutrinoSettings::P_MAIN_BLUE_BUTTON;
			button = SNeutrinoSettings::BUTTON_BLUE;
			break;
	}

	CNeutrinoApp::getInstance()->StopSubtitles();

	if (pers > -1 && (g_settings.personalize[pers] != CPersonalizeGui::PERSONALIZE_ACTIVE_MODE_ENABLED)) {
		ShowHint(LOCALE_MESSAGEBOX_INFO, g_Locale->getText(LOCALE_PERSONALIZE_MENUDISABLEDHINT),450, 10);
		CNeutrinoApp::getInstance()->StartSubtitles();
		return true;
	}

	width = w_max (40, 10);

	CMenuItem* menu_item = NULL;
	CColorKeyHelper keyhelper;
	
	//set default feature key
	neutrino_msg_t key = feat_key[CPersonalizeGui::PERSONALIZE_FEAT_KEY_AUTO].key; //CRCInput::RC_nokey
	
	const char * icon = NULL;
	int dummy;

	int menu_items = 0;
	int menu_prev = -1;

	// define classes
	CFavorites* tmpFavorites                                = NULL;
	CAudioSelectMenuHandler* tmpAudioSelectMenuHandler      = NULL;
	CMenuWidget* tmpNVODSelector                            = NULL;
	CSubChannelSelectMenu subchanselect;
	CStreamInfo2 * streamInfo				= NULL;
	CEventListHandler* tmpEventListHandler                  = NULL;
	CEPGplusHandler* tmpEPGplusHandler                      = NULL;
	CEPGDataHandler* tmpEPGDataHandler                      = NULL;
	CTimerList* Timerlist					= NULL;
	CRCLock *rcLock						= NULL;
	CStreamFeaturesChangeExec *StreamFeaturesChanger	= NULL;
	CImageInfo *imageinfo					= NULL;
	CDBoxInfoWidget *boxinfo				= NULL;
	CNeutrinoApp * neutrino					= NULL;
	CPluginList * games					= NULL;
	CPluginList * scripts					= NULL;
	
	std::string txt = g_settings.usermenu_text[button];
	if (button < COL_BUTTONMAX && txt.empty())
		txt = g_Locale->getText(user_menu[button].caption);

	CMenuWidget *menu = new CMenuWidget(txt, (button < COL_BUTTONMAX) ? user_menu[button].menu_icon_def : "", width);
	if (menu == NULL)
		return true;

	if (button < COL_BUTTONMAX)	
		menu->setSelected(user_menu[button].selected);
	
	//show cancel button if configured
	if (g_settings.personalize[SNeutrinoSettings::P_UMENU_SHOW_CANCEL])
		menu->addIntroItems(NONEXISTANT_LOCALE, NONEXISTANT_LOCALE, CMenuWidget::BTN_TYPE_CANCEL);
	else
		menu->addItem(GenericMenuSeparator);
	StreamFeaturesChanger     = new CStreamFeaturesChangeExec();
	
	// go through any postition number
	for (int pos = 0; pos < SNeutrinoSettings::ITEM_MAX ; pos++) {
		// now compare pos with the position of any item. Add this item if position is the same
		switch (g_settings.usermenu[button][pos]) {
		case SNeutrinoSettings::ITEM_NONE:
			// do nothing
			break;
		case SNeutrinoSettings::ITEM_BAR:
			if (menu_prev == -1 || menu_prev == SNeutrinoSettings::ITEM_BAR )
				break;
			menu->addItem(GenericMenuSeparatorLine);
			menu_prev = SNeutrinoSettings::ITEM_BAR;
			break;
		case SNeutrinoSettings::ITEM_FAVORITS:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_FAVORITS;
			tmpFavorites = new CFavorites;
			keyhelper.get(&key,&icon,feat_key[g_settings.personalize[SNeutrinoSettings::P_FEAT_KEY_FAVORIT]].key); //CRCInput::RC_green
			menu_item = new CMenuForwarder(LOCALE_FAVORITES_MENUEADD, true, NULL, tmpFavorites, "-1", key, icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_RECORD:
			if (g_settings.recording_type == RECORDING_OFF)
				break;

			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_RECORD;
			keyhelper.get(&key,&icon,CRCInput::RC_red);
			menu_item = new CMenuForwarder(LOCALE_MAINMENU_RECORDING, true, NULL, CRecordManager::getInstance(), "-1", key, icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_MOVIEPLAYER_MB:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_MOVIEPLAYER_MB;
			keyhelper.get(&key,&icon,CRCInput::RC_green);
			menu_item = new CMenuForwarder(LOCALE_MOVIEBROWSER_HEAD, true, NULL, CNeutrinoApp::getInstance(), "tsmoviebrowser", key, icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_TIMERLIST:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_TIMERLIST;
			keyhelper.get(&key,&icon,feat_key[g_settings.personalize[SNeutrinoSettings::P_FEAT_KEY_TIMERLIST]].key); //CRCInput::RC_yellow
			Timerlist = new CTimerList();
			menu_item = new CMenuForwarder(LOCALE_TIMERLIST_NAME, true, NULL, Timerlist, "-1", key, icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_REMOTE:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_REMOTE;
			keyhelper.get(&key,&icon,feat_key[g_settings.personalize[SNeutrinoSettings::P_FEAT_KEY_RC_LOCK]].key); //CRCInput::RC_nokey);
			rcLock = new CRCLock();
			menu_item = new CMenuForwarder(LOCALE_RCLOCK_MENUEADD, true, NULL, rcLock, "-1" , key, icon );
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_EPG_SUPER:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_EPG_SUPER;
			tmpEPGplusHandler = new CEPGplusHandler();
			keyhelper.get(&key,&icon,CRCInput::RC_green);
			menu_item = new CMenuForwarder(LOCALE_EPGMENU_EPGPLUS   , true, NULL, tmpEPGplusHandler  ,  "-1", key, icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_EPG_LIST:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_EPG_LIST;
			tmpEventListHandler = new CEventListHandler();
			keyhelper.get(&key,&icon,CRCInput::RC_red);
			menu_item = new CMenuForwarder(LOCALE_EPGMENU_EVENTLIST , true, NULL, tmpEventListHandler,  "-1", key, icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_EPG_INFO:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_EPG_INFO;
			tmpEPGDataHandler = new CEPGDataHandler();
			keyhelper.get(&key,&icon,CRCInput::RC_yellow);
			menu_item = new CMenuForwarder(LOCALE_EPGMENU_EVENTINFO , true, NULL, tmpEPGDataHandler ,  "-1", key, icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_EPG_MISC:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_EPG_MISC;
			dummy = g_Sectionsd->getIsScanningActive();
			//dummy = sectionsd_scanning;
			keyhelper.get(&key,&icon);
			menu_item = new CMenuOptionChooser(LOCALE_MAINMENU_PAUSESECTIONSD, &dummy, OPTIONS_OFF0_ON1_OPTIONS, OPTIONS_OFF0_ON1_OPTION_COUNT, true, this , key, icon );
			menu->addItem(menu_item, false);
			menu_items++;
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_MAINMENU_CLEARSECTIONSD, true, NULL, CNeutrinoApp::getInstance(), "clearSectionsd", key,icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_AUDIO_SELECT:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_AUDIO_SELECT;
			tmpAudioSelectMenuHandler = new CAudioSelectMenuHandler;
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_AUDIOSELECTMENUE_HEAD, true, NULL, tmpAudioSelectMenuHandler, "-1", key,icon);
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_SUBCHANNEL:
			if (!(g_RemoteControl->subChannels.empty())) {
				// NVOD/SubService- Kanal!
				tmpNVODSelector = new CMenuWidget(g_RemoteControl->are_subchannels ? LOCALE_NVODSELECTOR_SUBSERVICE : LOCALE_NVODSELECTOR_HEAD, NEUTRINO_ICON_VIDEO);
				if (subchanselect.getNVODMenu(tmpNVODSelector)) {
					menu_items++;
					menu_prev = SNeutrinoSettings::ITEM_SUBCHANNEL;
					keyhelper.get(&key,&icon);
					menu_item = new CMenuForwarder(g_RemoteControl->are_subchannels ? LOCALE_NVODSELECTOR_SUBSERVICE : LOCALE_NVODSELECTOR_HEAD, true, NULL, tmpNVODSelector, "-1", key,icon);
					menu->addItem(menu_item, false);
				}
			}
			break;
		case SNeutrinoSettings::ITEM_TECHINFO:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_TECHINFO;
			streamInfo = new CStreamInfo2();
			keyhelper.get(&key,&icon,CRCInput::RC_blue);
			menu_item = new CMenuForwarder(LOCALE_EPGMENU_STREAMINFO, !CNeutrinoApp::getInstance()->channelList->isEmpty(), NULL, streamInfo, "-1", key, icon );
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_GAMES:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_GAMES;
			games = new CPluginList(LOCALE_MAINMENU_GAMES,CPlugins::P_TYPE_GAME);
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_MAINMENU_GAMES, g_PluginList->hasPlugin(CPlugins::P_TYPE_GAME), NULL, games, "-1", key, icon );
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_SCRIPTS:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_SCRIPTS;
			scripts = new CPluginList(LOCALE_MAINMENU_SCRIPTS,CPlugins::P_TYPE_SCRIPT);
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_MAINMENU_SCRIPTS, g_PluginList->hasPlugin(CPlugins::P_TYPE_SCRIPT), NULL, scripts, "-1", key, icon );
			menu->addItem(menu_item, false);
			break;
		case SNeutrinoSettings::ITEM_PLUGIN:
		{
			char id[5];
			int cnt = 0;
			for (unsigned int count = 0; count < (unsigned int) g_PluginList->getNumberOfPlugins(); count++)
			{
				bool show = g_PluginList->getType(count) == CPlugins::P_TYPE_TOOL ||
					    g_PluginList->getType(count) == CPlugins::P_TYPE_LUA;
				if (show && !g_PluginList->isHidden(count))
				{
					sprintf(id, "%d", count);
					menu_items++;
					menu_prev = SNeutrinoSettings::ITEM_PLUGIN;
					neutrino_msg_t d_key = g_PluginList->getKey(count);
					//printf("[neutrino usermenu] plugin %d, set key %d...\n", count, g_PluginList->getKey(count));
					keyhelper.get(&key,&icon, d_key);
					menu_item = new CMenuForwarder(g_PluginList->getName(count), true, NULL, StreamFeaturesChanger, id, key, icon);

					menu->addItem(menu_item, 0);
					cnt++;
				}
			}
		}
		break;

		case SNeutrinoSettings::ITEM_VTXT:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_VTXT;
			keyhelper.get(&key,&icon, feat_key[g_settings.personalize[SNeutrinoSettings::P_FEAT_KEY_VTXT]].key); //CRCInput::RC_blue
			menu_item = new CMenuForwarder(LOCALE_USERMENU_ITEM_VTXT, true, NULL, StreamFeaturesChanger, "teletext", key, icon);
			menu->addItem(menu_item, 0);
			break;
		case SNeutrinoSettings::ITEM_IMAGEINFO:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_IMAGEINFO;
			imageinfo = new CImageInfo();
			keyhelper.get(&key,&icon);
			menu->addItem(new CMenuForwarder(LOCALE_SERVICEMENU_IMAGEINFO,  true, NULL, imageinfo, NULL, key, icon ), false);
			break;
		case SNeutrinoSettings::ITEM_BOXINFO:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_BOXINFO;
			boxinfo = new CDBoxInfoWidget();
			keyhelper.get(&key,&icon);
			menu->addItem( new CMenuForwarder(LOCALE_EXTRA_DBOXINFO, true, NULL, boxinfo, NULL, key, icon));
			break;
#if !HAVE_SPARK_HARDWARE
		case SNeutrinoSettings::ITEM_CAM:
			//if(cs_get_revision() != 10)
			{
				menu_items++;
				menu_prev = SNeutrinoSettings::ITEM_CAM;
				keyhelper.get(&key,&icon);
				menu->addItem(new CMenuForwarder(LOCALE_CI_SETTINGS, true, NULL, g_CamHandler, NULL, key, icon));
			}
			break;
#endif
		case SNeutrinoSettings::ITEM_CLOCK:
			{
				menu_items++;
				menu_prev = SNeutrinoSettings::ITEM_CLOCK;
				keyhelper.get(&key,&icon); 
				neutrino = CNeutrinoApp::getInstance();
				menu_item = new CMenuForwarder(!g_settings.mode_clock ? LOCALE_CLOCK_SWITCH_ON:LOCALE_CLOCK_SWITCH_OFF, true, NULL, neutrino, "clock_switch", key, icon);
				menu->addItem(menu_item, false);
			}
			break;
		case SNeutrinoSettings::ITEM_ADZAP:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_ADZAP;
			keyhelper.get(&key,&icon,CRCInput::RC_blue);
			menu_item = new CMenuForwarder(LOCALE_USERMENU_ITEM_ADZAP, true, NULL, CNeutrinoApp::getInstance()->AdZapChanger, "adzap", key, icon);
			menu->addItem(menu_item, 0);
			break;
		case SNeutrinoSettings::ITEM_EMU_RESTART:
			if (!access("/etc/init.d/cam", X_OK)) {
				menu_items++;
				menu_prev = SNeutrinoSettings::ITEM_EMU_RESTART;
				keyhelper.get(&key,&icon);
				menu_item = new CMenuForwarder(LOCALE_SERVICEMENU_RESTART_CAM, true, NULL, CNeutrinoApp::getInstance(), "restartcam", key, icon);
				menu->addItem(menu_item, 0);
			}
			break;
		case SNeutrinoSettings::ITEM_TUNER_RESTART:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_TUNER_RESTART;
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_SERVICEMENU_RESTART_TUNER, true, NULL, CNeutrinoApp::getInstance(), "restarttuner", key, icon);
			menu->addItem(menu_item, 0);
			break;
#if HAVE_SPARK_HARDWARE
		case SNeutrinoSettings::ITEM_THREE_D_MODE:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_THREE_D_MODE;
			keyhelper.get(&key,&icon);
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_THREE_D_SETTINGS, true, NULL, CNeutrinoApp::getInstance()->threeDSetup, "3dmode", key, icon);
			menu->addItem(menu_item, 0);
			break;
#endif
		case SNeutrinoSettings::ITEM_RASS:
			if (CNeutrinoApp::getInstance()->getMode() == CNeutrinoApp::mode_radio && g_Radiotext && g_Radiotext->haveRASS()) {
				menu_items++;
				menu_prev = SNeutrinoSettings::ITEM_RASS;
				keyhelper.get(&key,&icon);
				menu_item = new CMenuForwarder(LOCALE_RASS_HEAD, true, NULL, CNeutrinoApp::getInstance(), "rass", key, icon);
				menu->addItem(menu_item, 0);
			}
			break;
		case SNeutrinoSettings::ITEM_YOUTUBE:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_YOUTUBE;
			keyhelper.get(&key,&icon);
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_MOVIEPLAYER_YTPLAYBACK, true, NULL, CNeutrinoApp::getInstance(), "ytplayback", key, icon);
			menu->addItem(menu_item, 0);
			break;
		case SNeutrinoSettings::ITEM_NETZKINO:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_NETZKINO;
			keyhelper.get(&key,&icon);
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_MOVIEPLAYER_NKPLAYBACK, true, NULL, CNeutrinoApp::getInstance(), "nkplayback", key, icon);
			menu->addItem(menu_item, 0);
			break;
		case SNeutrinoSettings::ITEM_FILEPLAY:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_FILEPLAY;
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_MOVIEPLAYER_FILEPLAYBACK, true, NULL, CNeutrinoApp::getInstance(), "fileplayback", key, icon);
			menu_item->setHint(NEUTRINO_ICON_HINT_FILEPLAY, LOCALE_MENU_HINT_FILEPLAY);
			menu->addItem(menu_item, 0);
			break;
		case SNeutrinoSettings::ITEM_AUDIOPLAY:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_AUDIOPLAY;
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_AUDIOPLAYER_NAME, true, NULL, CNeutrinoApp::getInstance(), "audioplayer", key, icon);
			menu_item->setHint(NEUTRINO_ICON_HINT_APLAY, LOCALE_MENU_HINT_APLAY);
			menu->addItem(menu_item, 0);
			break;
		case SNeutrinoSettings::ITEM_INETPLAY:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_INETPLAY;
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_INETRADIO_NAME, true, NULL, CNeutrinoApp::getInstance(), "inetplayer", key, icon);
			menu_item->setHint(NEUTRINO_ICON_HINT_INET_RADIO, LOCALE_MENU_HINT_INET_RADIO);
			menu->addItem(menu_item, 0);
			break;
		case SNeutrinoSettings::ITEM_HDDMENU:
			menu_items++;
			menu_prev = SNeutrinoSettings::ITEM_HDDMENU;
			keyhelper.get(&key,&icon);
			menu_item = new CMenuForwarder(LOCALE_HDD_SETTINGS, true, NULL, CNeutrinoApp::getInstance(), "hddmenu", key, icon);
			menu_item->setHint(NEUTRINO_ICON_HINT_HDD, LOCALE_MENU_HINT_HDD);
			menu->addItem(menu_item, 0);
			break;
		default:
			printf("[neutrino] WARNING! menu wrong item!!\n");
			break;
		}
	}

#if 0
	// Allow some tailoring for privat image bakers ;)
	if (button == SNeutrinoSettings::BUTTON_RED) {
	}
	else if ( button == SNeutrinoSettings::BUTTON_GREEN) {
	}
	else if ( button == SNeutrinoSettings::BUTTON_YELLOW) {
	}
	else if ( button == SNeutrinoSettings::BUTTON_BLUE) {
	}
#endif

	extern CInfoClock *InfoClock;
	InfoClock->enableInfoClock(false);

	// show menu if there are more than 2 items only
	// otherwise, we start the item directly (must be the last one)
	if (menu_items > 1 ) {
		menu->exec(NULL,"");
	}
	else if (menu_item != NULL)
		menu_item->exec( NULL );
	
	InfoClock->enableInfoClock(true);
	CNeutrinoApp::getInstance()->StartSubtitles();

	if (button < COL_BUTTONMAX)
		user_menu[button].selected = menu->getSelected();

	// clear the heap
	if (tmpFavorites)                delete tmpFavorites;
	if (tmpAudioSelectMenuHandler)   delete tmpAudioSelectMenuHandler;
	if (tmpNVODSelector)             delete tmpNVODSelector;
	if (streamInfo)                  delete streamInfo;
	if (tmpEventListHandler)         delete tmpEventListHandler;
	if (tmpEPGplusHandler)           delete tmpEPGplusHandler;
	if (tmpEPGDataHandler)           delete tmpEPGDataHandler;
	if (Timerlist)			 delete Timerlist;
	if (rcLock)			 delete rcLock;
	if (StreamFeaturesChanger)	 delete StreamFeaturesChanger;
	if (imageinfo)			 delete imageinfo;
	if (boxinfo)			 delete boxinfo;
	if (games)                       delete games;
	if (scripts)                     delete scripts;
	if (menu)                        delete menu;
 	return true;
}

const char *CUserMenu::getUserMenuButtonName(int button, bool &active)
{
	active = false;
        if(button < 0 || button >= SNeutrinoSettings::BUTTON_MAX)
                return false;

	bool return_title = false;
	neutrino_locale_t loc = NONEXISTANT_LOCALE;
	char *text = NULL;

	#define locCheck(L) if(loc != NONEXISTANT_LOCALE) return_title = true; else loc=L;active=true;continue

        for(int pos = 0; pos < SNeutrinoSettings::ITEM_MAX && !return_title; pos++) {
                switch(g_settings.usermenu[button][pos]) {
                        case SNeutrinoSettings::ITEM_NONE:
                        case SNeutrinoSettings::ITEM_BAR:
				continue;
                        case SNeutrinoSettings::ITEM_EPG_LIST:
				locCheck(LOCALE_EPGMENU_EVENTLIST);
                        case SNeutrinoSettings::ITEM_EPG_SUPER:
				locCheck(LOCALE_EPGMENU_EPGPLUS);
                        case SNeutrinoSettings::ITEM_EPG_INFO:
                                locCheck(LOCALE_EPGMENU_EVENTINFO);
                        case SNeutrinoSettings::ITEM_EPG_MISC:
				return_title = true; active = true; continue;
                        case SNeutrinoSettings::ITEM_AUDIO_SELECT:
				if (g_RemoteControl->current_PIDs.APIDs.size() > 0)
                        		text = g_RemoteControl->current_PIDs.APIDs[
						g_RemoteControl->current_PIDs.PIDs.selected_apid].desc;
				locCheck(LOCALE_AUDIOSELECTMENUE_HEAD);
			case SNeutrinoSettings::ITEM_SUBCHANNEL:
				if (!g_RemoteControl->subChannels.empty()) {
					locCheck(g_RemoteControl->are_subchannels ? LOCALE_NVODSELECTOR_SUBSERVICE : LOCALE_NVODSELECTOR_HEAD);
				}
				continue;
                        case SNeutrinoSettings::ITEM_RECORD:
				locCheck(LOCALE_FAVORITES_MENUEADD);
                        case SNeutrinoSettings::ITEM_MOVIEPLAYER_MB:
                                locCheck(LOCALE_MOVIEBROWSER_HEAD);
                        case SNeutrinoSettings::ITEM_TIMERLIST:
                                locCheck(LOCALE_TIMERLIST_NAME);
                        case SNeutrinoSettings::ITEM_FAVORITS:
				locCheck(LOCALE_FAVORITES_MENUEADD);
			case SNeutrinoSettings::ITEM_VTXT:
				locCheck(LOCALE_USERMENU_ITEM_VTXT);
                        case SNeutrinoSettings::ITEM_TECHINFO:
                                locCheck(LOCALE_EPGMENU_STREAMINFO);
                        case SNeutrinoSettings::ITEM_REMOTE:
                                locCheck(LOCALE_RCLOCK_MENUEADD);
                        case SNeutrinoSettings::ITEM_PLUGIN:
				return_title = true;
				continue;
                        case SNeutrinoSettings::ITEM_IMAGEINFO:
                                locCheck(LOCALE_SERVICEMENU_IMAGEINFO);
                        case SNeutrinoSettings::ITEM_BOXINFO:
				locCheck(LOCALE_EXTRA_DBOXINFO);
                        case SNeutrinoSettings::ITEM_CAM:
				locCheck(LOCALE_CI_SETTINGS);
                        case SNeutrinoSettings::ITEM_CLOCK:
				locCheck(!g_settings.mode_clock ? LOCALE_CLOCK_SWITCH_ON:LOCALE_CLOCK_SWITCH_OFF);
                        case SNeutrinoSettings::ITEM_GAMES:
				locCheck(LOCALE_MAINMENU_GAMES);
                        case SNeutrinoSettings::ITEM_SCRIPTS:
				locCheck(LOCALE_MAINMENU_SCRIPTS);
			case SNeutrinoSettings::ITEM_ADZAP:
				locCheck(LOCALE_USERMENU_ITEM_ADZAP);
                        case SNeutrinoSettings::ITEM_EMU_RESTART:
				locCheck(LOCALE_SERVICEMENU_RESTART_CAM);
                        case SNeutrinoSettings::ITEM_TUNER_RESTART:
				locCheck(LOCALE_SERVICEMENU_RESTART_TUNER);
                        case SNeutrinoSettings::ITEM_THREE_D_MODE:
				locCheck(LOCALE_THREE_D_SETTINGS);
                        case SNeutrinoSettings::ITEM_YOUTUBE:
				locCheck(LOCALE_MOVIEPLAYER_YTPLAYBACK);
                        case SNeutrinoSettings::ITEM_NETZKINO:
				locCheck(LOCALE_MOVIEPLAYER_NKPLAYBACK);
                        case SNeutrinoSettings::ITEM_FILEPLAY:
				locCheck(LOCALE_MOVIEPLAYER_FILEPLAYBACK);
                        case SNeutrinoSettings::ITEM_HDDMENU:
				locCheck(LOCALE_HDD_SETTINGS);
                }
        }

	if (!return_title && (loc != NONEXISTANT_LOCALE)) {
		if(text)
			return text;
		return g_Locale->getText(loc);
	}
	if (return_title && g_settings.usermenu_text[button].length())
		return g_settings.usermenu_text[button].c_str();
	if (return_title)
		return g_Locale->getText(usermenu[button].def_name);
	return "";
}

/**************************************************************************************
*          changeNotify - features menu recording start / stop                        *
**************************************************************************************/
bool CUserMenu::changeNotify(const neutrino_locale_t OptionName, void * Data)
{
	if (ARE_LOCALES_EQUAL(OptionName, LOCALE_MAINMENU_PAUSESECTIONSD)) {
		g_Sectionsd->setPauseScanning((*((int *)Data)) == 0);
	}
	
	return false;
}
