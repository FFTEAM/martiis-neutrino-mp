/*
	KernelOptions Menu

	Copyright (C) 2012 martii

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <config.h>
#include <global.h>
#include <neutrino.h>
#include <sys/stat.h>
#include <system/debug.h>
#include <system/helpers.h>
#include <driver/screen_max.h>
#include <gui/widget/messagebox.h>
#include <gui/widget/menue.h>
#include <gui/kerneloptions.h>

#define ONOFF_OPTION_COUNT 2
static const CMenuOptionChooser::keyval ONOFF_OPTIONS[ONOFF_OPTION_COUNT] = {
	{ 0, LOCALE_OPTIONS_OFF },
	{ 1, LOCALE_OPTIONS_ON }
};

KernelOptions_Menu::KernelOptions_Menu()
{
	width = w_max (40, 10);
}

void KernelOptions_Menu::loadModule(int i)
{
	// check whether there are dependencies with options specified
	unsigned int j;
	for (j = 0; j < modules[i].moduleList.size(); j++)
		if (!modules[i].moduleList[j].second.empty())
			break;

	if (j >= modules[i].moduleList.size() - 1) {
		// dependencies come without options
		if (modules[i].moduleList.back().second.empty())
			my_system(2, "modprobe",  modules[i].moduleList.back().first.c_str());
		else
			system(("modprobe " + modules[i].moduleList.back().first + " " + modules[i].moduleList.back().second).c_str());
		return;
	}

	for (j = 0; j < modules[i].moduleList.size(); j++) {
		if (modules[i].moduleList.back().second.empty())
			my_system(2, "modprobe",  modules[i].moduleList[j].first.c_str());
		else
			system(("modprobe " + modules[i].moduleList[j].first + " " + modules[i].moduleList.back().second).c_str());
	}
}

void KernelOptions_Menu::unloadModule(int i)
{
	my_system(3, "modprobe", "-r", modules[i].moduleList.back().first.c_str());
}

int KernelOptions_Menu::exec(CMenuTarget* parent, const std::string & actionKey)
{
	int res = menu_return::RETURN_REPAINT;

	if (actionKey == "reset") {
		for (unsigned int i = 0; i < modules.size(); i++)
			modules[i].active = modules[i].active_orig;
		return res;
	}

	if (actionKey == "apply" || actionKey == "change") {
		bool needs_save = false;
		for (unsigned int i = 0; i < modules.size(); i++)
			if (modules[i].active != modules[i].active_orig) {
				needs_save = true;
				if (modules[i].active)
					loadModule(i);
				else
					unloadModule(i);
				modules[i].active_orig = modules[i].active;
			}
		if (needs_save)
			save();
		if (actionKey == "change")
			return res; // whatever
	}

	if (actionKey == "apply" || actionKey == "lsmod") {
		for (unsigned int i = 0; i < modules.size(); i++)
			modules[i].installed = false;
		FILE *f = fopen("/proc/modules", "r");
		if (f) {
			char buf[200];
			while (fgets(buf, sizeof(buf), f)) {
				char name[sizeof(buf)];
				if (1 == sscanf(buf, "%s", name))
					for (unsigned int i = 0; i < modules.size(); i++) {
						if (name == modules[i].moduleList.back().first) {
							modules[i].installed = true;
							break;
						}
				}
			}
			fclose(f);
		}

		std::string text = "";
		for (unsigned int i = 0; i < modules.size(); i++) {
			text += modules[i].comment + " (" + modules[i].moduleList.back().first + "): ";
			text += g_Locale->getText(modules[i].active ? LOCALE_KERNELOPTIONS_ENABLED : LOCALE_KERNELOPTIONS_DISABLED);
			text += ", ";
			text += g_Locale->getText(modules[i].installed ? LOCALE_KERNELOPTIONS_LOADED : LOCALE_KERNELOPTIONS_NOT_LOADED);
			text += "\n";
		}

		ShowMsg(LOCALE_KERNELOPTIONS_LSMOD, text, CMessageBox::mbrBack, CMessageBox::mbBack, NEUTRINO_ICON_INFO);

		return res;
	}

	if (parent)
		parent->hide();

	Settings();

	return res;
}

void KernelOptions_Menu::hide()
{
}

bool KernelOptions_Menu::isEnabled(std::string name) {
	load();
	for (unsigned int i = 0; i < modules.size(); i++)
		if (name == modules[i].moduleList.back().first)
			return modules[i].active;
	return false;
}

bool KernelOptions_Menu::Enable(std::string name, bool active) {
	load();
	for (unsigned int i = 0; i < modules.size(); i++)
		if (name == modules[i].moduleList.back().first) {
				if (modules[i].active != active) {
					modules[i].active = active;
					exec(NULL, "change");
				}
				return true;
		}
	return false;
}

void KernelOptions_Menu::load() {
	modules.clear();

	FILE *f = fopen("/etc/modules.available", "r");
	// Syntax:
	//
	// # comment
	// module # description
	// module module module # description
	// module module(arguments) module # description
	//

	if (f) {
		char buf[200];
		while (fgets(buf, sizeof(buf), f)) {
			if (buf[0] == '#')
				continue;
			char *comment = strchr(buf, '#');
			if (!comment)
				continue;
			*comment++ = 0;
			while (*comment == ' ' || *comment == '\t')
				comment++;
			if (strlen(comment) < 1)
				continue;
			module m;
			m.active = m.active_orig = 0;
			m.installed = false;
			char *nl = strchr(comment, '\n');
			if (nl)
				*nl = 0;
			m.comment = std::string(comment);
			char *b = buf;
			while (*b) {
				if (*b == ' ' || *b == '\t') {
					b++;
					continue;
				}
				std::string args = "";
				std::string mod;
				char *e = b;
				char *a = NULL;
				while (*e && ((a && *e != ')') || (!a && *e != ' ' && *e != '\t'))) {
					if (*e == '(')
						a = e;
					e++;
				}
				if (a && *e == ')') {
					*a++ = 0;
					*e++ = 0;
					args = std::string(a);
					*a = 0;
					mod = std::string(b);
					b = e;
				} else if (*e) {
					*e++ = 0;
					mod = std::string(b);
					b = e;
				} else {
					mod = std::string(b);
					b = e;
				}
				m.moduleList.push_back(make_pair(mod, args));
			}
			if (m.moduleList.size() > 0)
				modules.push_back(m);
		}
		fclose(f);
	}

	f = fopen("/etc/modules.extra", "r");
	if (f) {
		char buf[200];
		while (fgets(buf, sizeof(buf), f)) {
			char *t = strchr(buf, '#');
			if (t)
				*t = 0;
			char name[200];
			if (1 == sscanf(buf, "%s", name)) {
				for (unsigned int i = 0; i < modules.size(); i++)
					if (modules[i].moduleList.back().first == name) {
						modules[i].active = modules[i].active_orig = 1;
						break;
					}
			}
		}
		fclose(f);
	}
}

void KernelOptions_Menu::save()
{
	FILE *f = fopen("/etc/modules.extra", "w");
	if (f) {
		chmod("/etc/modules.extra", 0644);
		for (unsigned int i = 0; i < modules.size(); i++) {
			if (modules[i].active) {
				for (unsigned int j = 0; j < modules[i].moduleList.size(); j++)
					if (modules[i].moduleList[j].second.length())
						fprintf(f, "%s %s\n",
							modules[i].moduleList[j].first.c_str(),
							modules[i].moduleList[j].second.c_str());
					else
						fprintf(f, "%s\n",
							modules[i].moduleList[j].first.c_str());
			}
		}
		fclose(f);
	}
}

void KernelOptions_Menu::Settings()
{
	CMenuWidget* menu = new CMenuWidget(LOCALE_KERNELOPTIONS_HEAD, "settings");
	menu->addItem(GenericMenuSeparator);
	menu->addItem(GenericMenuBack);
	menu->addItem(new CMenuSeparator(CMenuSeparator::LINE | CMenuSeparator::STRING, LOCALE_KERNELOPTIONS_MODULES));

	load();

	for (unsigned int i = 0; i < modules.size(); i++)
		menu->addItem(new CMenuOptionChooser(modules[i].comment.c_str(), &modules[i].active,
				ONOFF_OPTIONS, ONOFF_OPTION_COUNT, true));

	menu->addItem(GenericMenuSeparatorLine);

	menu->addItem(new CMenuForwarder(LOCALE_KERNELOPTIONS_RESET, true, NULL, this,
		"reset", CRCInput::RC_red, NEUTRINO_ICON_BUTTON_RED));
	menu->addItem(new CMenuForwarder(LOCALE_KERNELOPTIONS_APPLY, true, NULL, this,
		"apply", CRCInput::RC_green, NEUTRINO_ICON_BUTTON_GREEN));
	menu->addItem(new CMenuForwarder(LOCALE_KERNELOPTIONS_LSMOD, true, NULL, this,
		"lsmod", CRCInput::RC_yellow, NEUTRINO_ICON_BUTTON_YELLOW));
	menu->exec (NULL, "");
	menu->hide ();
	delete menu;
}
// vim:ts=4
