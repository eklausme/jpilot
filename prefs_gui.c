/* prefs_gui.c
 *
 * Copyright (C) 1999 by Judd Montgomery
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include "i18n.h"
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "prefs.h"
#include "prefs_gui.h"
#include "log.h"
#include "plugins.h"

static GtkWidget *window;
static GtkWidget *main_window;
static GtkWidget *port_entry;
static GtkWidget *backups_entry;
static GtkWidget *alarm_command_entry;

extern int glob_app;

#ifdef COLORS
/* This doesn't work quite right.  I don't know why. */
void set_colors()
{
   read_gtkrc_file();
   gtk_rc_reparse_all();
   gtk_widget_reset_rc_styles(window);
   gtk_widget_reset_rc_styles(main_window);
   gtk_widget_queue_draw(window);
   gtk_widget_queue_draw(main_window);
}
#endif
static void cb_pref_menu(GtkWidget *widget,
			 gpointer   data)
{
   int pref;
   int value;
   
   if (!widget)
     return;
   if (!(GTK_CHECK_MENU_ITEM(widget))->active) {
      return;
   }

   pref = GPOINTER_TO_INT(data);
   value = pref & 0xFF;
   pref = pref >> 8;
   set_pref(pref, value);
   jpilot_logf(LOG_DEBUG, "pref %d, value %d\n", pref, value);
#ifdef COLORS
   if (pref==PREF_RCFILE) {
      set_colors();
   }
#endif
   return;
}
   

static int make_pref_menu(GtkWidget **pref_menu, int pref_num)
{
   GtkWidget *menu_item;
   GtkWidget *menu;
   GSList    *group;
   int i, r;
   long ivalue;
   const char *svalue;
   char format_text[MAX_PREF_VALUE];
   char human_text[MAX_PREF_VALUE];
   time_t ltime;
   struct tm *now;
      
   time(&ltime);
   now = localtime(&ltime);

   *pref_menu = gtk_option_menu_new();
   
   menu = gtk_menu_new();
   group = NULL;
   
   get_pref(pref_num, &ivalue, &svalue);
	    
   for (i=0; i<1000; i++) {
      r = get_pref_possibility(pref_num, i, format_text);
      if (r) {
	 break;
      }
      switch (pref_num) {
       case PREF_SHORTDATE:
       case PREF_LONGDATE:
       case PREF_TIME:
	 strftime(human_text, MAX_PREF_VALUE, format_text, now);
	 break;
       default:
	 strncpy(human_text, format_text, MAX_PREF_VALUE);
	 break;
      }
      menu_item = gtk_radio_menu_item_new_with_label(
		     group, human_text);
      gtk_signal_connect(GTK_OBJECT(menu_item), "activate", cb_pref_menu,
			 GINT_TO_POINTER(((pref_num*0x100) + (i & 0xFF))));
      group = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(menu_item));
      gtk_menu_append(GTK_MENU(menu), menu_item);
      
      if (ivalue == i) {
	 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item), ivalue);
      }

      gtk_widget_show(menu_item);
   }
   gtk_option_menu_set_menu(GTK_OPTION_MENU(*pref_menu), menu);
   
   return 0;
}

void cb_show_deleted(GtkWidget *widget,
		     gpointer data)
{
   set_pref(PREF_SHOW_DELETED, GTK_TOGGLE_BUTTON(widget)->active);
}
void cb_show_modified(GtkWidget *widget,
		      gpointer data)
{
   set_pref(PREF_SHOW_MODIFIED, GTK_TOGGLE_BUTTON(widget)->active);
}
void cb_highlight(GtkWidget *widget,
		  gpointer data)
{
   set_pref(PREF_HIGHLIGHT, GTK_TOGGLE_BUTTON(widget)->active);
}
void cb_use_db3(GtkWidget *widget,
		gpointer data)
{
   set_pref(PREF_USE_DB3, GTK_TOGGLE_BUTTON(widget)->active);
}
void cb_sync_datebook(GtkWidget *widget,
		      gpointer data)
{
   set_pref(PREF_SYNC_DATEBOOK, GTK_TOGGLE_BUTTON(widget)->active);
}
void cb_sync_address(GtkWidget *widget,
		     gpointer data)
{
   set_pref(PREF_SYNC_ADDRESS, GTK_TOGGLE_BUTTON(widget)->active);
}
void cb_sync_todo(GtkWidget *widget,
		  gpointer data)
{
   set_pref(PREF_SYNC_TODO, GTK_TOGGLE_BUTTON(widget)->active);
}
void cb_sync_memo(GtkWidget *widget,
		  gpointer data)
{
   set_pref(PREF_SYNC_MEMO, GTK_TOGGLE_BUTTON(widget)->active);
}
#ifdef ENABLE_PLUGINS
void cb_sync_plugin(GtkWidget *widget,
		    gpointer data)
{
   GList *plugin_list, *temp_list;
   struct plugin_s *Pplugin;
   int number;
   
   number = GPOINTER_TO_INT(data);
   
   plugin_list=NULL;
   
   plugin_list = get_plugin_list();
   
   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      Pplugin = (struct plugin_s *)temp_list->data;
      if (Pplugin) {
	 if (number == Pplugin->number) {
	    if (GTK_TOGGLE_BUTTON(widget)->active) {
	       Pplugin->sync_on = 1;
	    } else {
	       Pplugin->sync_on = 0;
	    }
	 }
      }
   }
   write_plugin_sync_file();
}
#endif
void cb_open_alarm(GtkWidget *widget,
		   gpointer data)
{
   set_pref(PREF_OPEN_ALARM_WINDOWS, GTK_TOGGLE_BUTTON(widget)->active);
}
void cb_do_command(GtkWidget *widget,
		   gpointer data)
{
   set_pref(PREF_DO_ALARM_COMMAND, GTK_TOGGLE_BUTTON(widget)->active);
}



static gboolean cb_destroy(GtkWidget *widget)
{
   char *entry_text;
   char *backups_text;
   int num_backups;
   
   jpilot_logf(LOG_DEBUG, "Cleanup\n");

   entry_text = gtk_entry_get_text(GTK_ENTRY(port_entry));
   jpilot_logf(LOG_DEBUG, "port_entry = [%s]\n", entry_text);
   set_pref_char(PREF_PORT, entry_text);
   
   entry_text = gtk_entry_get_text(GTK_ENTRY(alarm_command_entry));
   jpilot_logf(LOG_DEBUG, "alarm_command_entry = [%s]\n", entry_text);
   set_pref_char(PREF_ALARM_COMMAND, entry_text);
   
   backups_text = gtk_entry_get_text(GTK_ENTRY(backups_entry));
   jpilot_logf(LOG_DEBUG, "backups_entry = [%s]\n", backups_text);
   num_backups = atoi(backups_text);
   if (num_backups < 1) {
      num_backups = 1;
   }
   if (num_backups > 99) {
      num_backups = 99;
   }
   set_pref(PREF_NUM_BACKUPS, num_backups);
   
   write_rc_file();

   window = NULL;
   if (glob_app==DATEBOOK) {
      cb_app_button(NULL, GINT_TO_POINTER(REDRAW));
   }
   return FALSE;
}

static void
  cb_quit(GtkWidget *widget,
	   gpointer   data)
{
   jpilot_logf(LOG_DEBUG, "cb_quit\n");
   if (GTK_IS_WIDGET(data)) {
      gtk_widget_destroy(data);
   }
}

void cb_prefs_gui(GtkWidget *widget, gpointer data)
{
   GtkWidget *checkbutton;
   GtkWidget *pref_menu;
   GtkWidget *label;
   GtkWidget *button;
   GtkWidget *table;
   GtkWidget *vbox;
   GtkWidget *vbox_locale;
   GtkWidget *vbox_settings;
   GtkWidget *vbox_alarms;
   GtkWidget *vbox_conduits;
   GtkWidget *hbox_temp;
   GtkWidget *notebook;
   long ivalue;
   const char *cstr;
   char temp_str[10];
   char temp[256];
#ifdef ENABLE_PLUGINS
   GList *plugin_list, *temp_list;
   struct plugin_s *Pplugin;
   extern unsigned char skip_plugins;
#endif
   
   jpilot_logf(LOG_DEBUG, "cb_prefs_gui\n");
   if (window) {
      jpilot_logf(LOG_DEBUG, "pref_window is already up\n");
      return;
   }

   main_window = data;

   window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   /*gtk_window_set_default_size(GTK_WINDOW(window), 500, 300); */

   gtk_container_set_border_width(GTK_CONTAINER(window), 10);
   g_snprintf(temp, 255, "%s %s", PN, _("Preferences"));
   temp[255]='\0';
   gtk_window_set_title(GTK_WINDOW(window), temp);

   gtk_signal_connect(GTK_OBJECT(window), "destroy",
                      GTK_SIGNAL_FUNC(cb_destroy), window);

   vbox = gtk_vbox_new(FALSE, 5);
   gtk_container_add(GTK_CONTAINER(window), vbox);

   vbox_locale = gtk_vbox_new(FALSE, 0);
   vbox_settings = gtk_vbox_new(FALSE, 0);
   vbox_alarms = gtk_vbox_new(FALSE, 0);
   vbox_conduits = gtk_vbox_new(FALSE, 0);

   gtk_container_set_border_width(GTK_CONTAINER(vbox_locale), 5);
   gtk_container_set_border_width(GTK_CONTAINER(vbox_settings), 5);
   gtk_container_set_border_width(GTK_CONTAINER(vbox_alarms), 5);
   gtk_container_set_border_width(GTK_CONTAINER(vbox_conduits), 5);

   /*Add the notebook for repeat types */
   notebook = gtk_notebook_new();
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
   label = gtk_label_new(_("Locale"));
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_locale, label);
   label = gtk_label_new(_("Settings"));
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_settings, label);
   label = gtk_label_new(_("Alarms"));
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_alarms, label);
   label = gtk_label_new(_("Conduits"));
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_conduits, label);
   gtk_box_pack_start(GTK_BOX(vbox), notebook, FALSE, FALSE, 0);

   /* Table for Locale */
   table = gtk_table_new(5, 2, FALSE);
   gtk_table_set_row_spacings(GTK_TABLE(table),0);
   gtk_table_set_col_spacings(GTK_TABLE(table),0);
   gtk_box_pack_start(GTK_BOX(vbox_locale), table, FALSE, FALSE, 0);

   /* Character Set */
   label = gtk_label_new(_("Character Set "));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 0, 1);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   make_pref_menu(&pref_menu, PREF_CHAR_SET);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(pref_menu),
			     1, 2, 0, 1);

   get_pref(PREF_CHAR_SET, &ivalue, &cstr);
   gtk_option_menu_set_history(GTK_OPTION_MENU(pref_menu), ivalue);

   /* Shortdate */
   label = gtk_label_new(_("Short date format "));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 1, 2);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   make_pref_menu(&pref_menu, PREF_SHORTDATE);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(pref_menu),
			     1, 2, 1, 2);

   get_pref(PREF_SHORTDATE, &ivalue, &cstr);
   gtk_option_menu_set_history(GTK_OPTION_MENU(pref_menu), ivalue);

   /* Longdate */
   label = gtk_label_new(_("Long date format "));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 2, 3);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   make_pref_menu(&pref_menu, PREF_LONGDATE);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(pref_menu),
			     1, 2, 2, 3);

   get_pref(PREF_LONGDATE, &ivalue, &cstr);
   gtk_option_menu_set_history(GTK_OPTION_MENU(pref_menu), ivalue);


   /* Time */
   label = gtk_label_new(_("Time format "));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 3, 4);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   make_pref_menu(&pref_menu, PREF_TIME);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(pref_menu),
			     1, 2, 3, 4);

   get_pref(PREF_TIME, &ivalue, &cstr);
   gtk_option_menu_set_history(GTK_OPTION_MENU(pref_menu), ivalue);


   /* FDOW */
   label = gtk_label_new(_("The first day of the week is "));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 4, 5);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   make_pref_menu(&pref_menu, PREF_FDOW);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(pref_menu),
			     1, 2, 4, 5);

   get_pref(PREF_FDOW, &ivalue, &cstr);
   gtk_option_menu_set_history(GTK_OPTION_MENU(pref_menu), ivalue);


   /* Table for Settings*/
   table = gtk_table_new(4, 2, FALSE);
   gtk_table_set_row_spacings(GTK_TABLE(table),0);
   gtk_table_set_col_spacings(GTK_TABLE(table),0);
   gtk_box_pack_start(GTK_BOX(vbox_settings), table, FALSE, FALSE, 0);

   /* GTK colors file */
   label = gtk_label_new(_("My GTK colors file is "));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 0, 1);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   make_pref_menu(&pref_menu, PREF_RCFILE);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(pref_menu),
			     1, 2, 0, 1);
   
   get_pref(PREF_RCFILE, &ivalue, &cstr);
   gtk_option_menu_set_history(GTK_OPTION_MENU(pref_menu), ivalue);


   /* Port */
   label = gtk_label_new(_("Serial Port (/dev/ttyS0, /dev/pilot)"));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 1, 2);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   port_entry = gtk_entry_new_with_max_length(MAX_PREF_VALUE - 2);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(port_entry),
			     1, 2, 1, 2);
   get_pref(PREF_PORT, &ivalue, &cstr);
   if (cstr) {
      gtk_entry_set_text(GTK_ENTRY(port_entry), cstr);
   }


   /* Rate */
   label = gtk_label_new(_("Serial Rate "));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 2, 3);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   make_pref_menu(&pref_menu, PREF_RATE);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(pref_menu),
			     1, 2, 2, 3);

   get_pref(PREF_RATE, &ivalue, &cstr);
   gtk_option_menu_set_history(GTK_OPTION_MENU(pref_menu), ivalue);

   /* Number of backups */
   label = gtk_label_new(_("Number of backups to be archived"));
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(label),
			     0, 1, 3, 4);
   gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);

   backups_entry = gtk_entry_new_with_max_length(2);
   gtk_widget_set_usize(backups_entry, 30, 0);
   gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(backups_entry),
			     1, 2, 3, 4);
   get_pref(PREF_NUM_BACKUPS, &ivalue, &cstr);
   sprintf(temp_str, "%ld", ivalue);
   gtk_entry_set_text(GTK_ENTRY(backups_entry), temp_str);


   /*Show deleted files check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Show deleted records (default NO)"));
   gtk_box_pack_start(GTK_BOX(vbox_settings), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_SHOW_DELETED, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton),
		      "clicked", GTK_SIGNAL_FUNC(cb_show_deleted),
		      GINT_TO_POINTER(PREF_SHOW_DELETED));

   /*Show modified files check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Show modified deleted records (default NO)"));
   gtk_box_pack_start(GTK_BOX(vbox_settings), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_SHOW_MODIFIED, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_show_modified),
		      GINT_TO_POINTER(PREF_SHOW_MODIFIED));


   /*Show highlight days check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Highlight calendar days with appointments"));
   gtk_box_pack_start(GTK_BOX(vbox_settings), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_HIGHLIGHT, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_highlight), NULL);


   /*Show use DateBk3/4 check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Use DateBk3/4 note tags"));
   gtk_box_pack_start(GTK_BOX(vbox_settings), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_USE_DB3, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_use_db3), NULL);



   /*Show sync datebook check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Sync datebook"));
   gtk_box_pack_start(GTK_BOX(vbox_conduits), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_SYNC_DATEBOOK, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_sync_datebook), NULL);

   /*Show sync address check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Sync address"));
   gtk_box_pack_start(GTK_BOX(vbox_conduits), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_SYNC_ADDRESS, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_sync_address), NULL);

   /*Show sync todo check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Sync todo"));
   gtk_box_pack_start(GTK_BOX(vbox_conduits), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_SYNC_TODO, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_sync_todo), NULL);

   /*Show sync memo check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Sync memo"));
   gtk_box_pack_start(GTK_BOX(vbox_conduits), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_SYNC_MEMO, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_sync_memo), NULL);

#ifdef  ENABLE_PLUGINS
   if (!skip_plugins) {
     
      plugin_list=NULL;

      plugin_list = get_plugin_list();

      for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
	 Pplugin = (struct plugin_s *)temp_list->data;
	 if (Pplugin) {
	    /* Make a checkbox for each plugin */
	    g_snprintf(temp, 250, "Sync %s (%s)", Pplugin->name, Pplugin->full_path);
	    temp[250]='\0';
	    checkbutton = gtk_check_button_new_with_label(temp);
	    gtk_box_pack_start(GTK_BOX(vbox_conduits), checkbutton, FALSE, FALSE, 0);
	    gtk_widget_show(checkbutton);
	    if (Pplugin->sync_on) {
	       gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
	    }
	    gtk_signal_connect(GTK_OBJECT(checkbutton), "clicked", 
			       GTK_SIGNAL_FUNC(cb_sync_plugin),
			       GINT_TO_POINTER(Pplugin->number));
	 }
      }
   }

#endif


   /* Alarms Preferences */
   /* Show open alarm windows check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Open alarm windows for appointment reminders"));
   gtk_box_pack_start(GTK_BOX(vbox_alarms), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_OPEN_ALARM_WINDOWS, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_open_alarm), NULL);

   /* Show open alarm windows check box */
   checkbutton = gtk_check_button_new_with_label
     (_("Execute this command"));
   gtk_box_pack_start(GTK_BOX(vbox_alarms), checkbutton, FALSE, FALSE, 0);
   get_pref(PREF_DO_ALARM_COMMAND, &ivalue, &cstr);
   gtk_widget_show(checkbutton);
   if (ivalue) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);
   }
   gtk_signal_connect(GTK_OBJECT(checkbutton), 
		      "clicked", GTK_SIGNAL_FUNC(cb_do_command), NULL);

   /* Shell warning */
   label = gtk_label_new(_("WARNING: executing arbitrary shell commands can be dangerous!!!"));
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
   gtk_box_pack_start(GTK_BOX(vbox_alarms), label, FALSE, FALSE, 0);

   /* Alarm Command */
   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox_alarms), hbox_temp, FALSE, FALSE, 0);

   label = gtk_label_new(_("Alarm Command"));
   gtk_box_pack_start(GTK_BOX(hbox_temp), label, FALSE, FALSE, 10);

   alarm_command_entry = gtk_entry_new_with_max_length(MAX_PREF_VALUE - 2);
   get_pref(PREF_ALARM_COMMAND, &ivalue, &cstr);
   if (cstr) {
      gtk_entry_set_text(GTK_ENTRY(alarm_command_entry), cstr);
   }
   gtk_box_pack_start(GTK_BOX(hbox_temp), alarm_command_entry, FALSE, FALSE, 0);

   label = gtk_label_new(_("%t is replaced with the alarm time"));
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
   gtk_box_pack_start(GTK_BOX(vbox_alarms), label, FALSE, FALSE, 0);

   label = gtk_label_new(_("%d is replaced with the alarm date"));
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
   gtk_box_pack_start(GTK_BOX(vbox_alarms), label, FALSE, FALSE, 0);

#ifdef ALARM_SHELL_DESC_NOTE
   label = gtk_label_new(_("%D is replaced with the alarm description"));
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
   gtk_box_pack_start(GTK_BOX(vbox_alarms), label, FALSE, FALSE, 0);

   label = gtk_label_new(_("%N is replaced with the alarm note"));
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
   gtk_box_pack_start(GTK_BOX(vbox_alarms), label, FALSE, FALSE, 0);
#endif

   /* Create a "Done" button */
   button = gtk_button_new_with_label(_("Done"));
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_quit), window);
   gtk_box_pack_end(GTK_BOX(vbox), button, FALSE, FALSE, 0);

   gtk_widget_show_all(window);
}
