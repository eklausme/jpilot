/* jpilot.c
 * 
 * Copyright (C) 1999 by Judd Montgomery
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <gtk/gtk.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include <pi-datebook.h>
#include <gdk/gdkkeysyms.h>

#include "datebook.h"
#include "address.h"
#include "todo.h"
#include "memo.h"
#include "libplugin.h"
#include "utils.h"
#include "sync.h"
#include "log.h"
#include "prefs_gui.h"
#include "prefs.h"
#include "plugins.h"
#define ALARMS
#ifdef ALARMS
#include "alarms.h"
#endif
#include "print.h"
#include "password.h"
#include "i18n.h"

#ifndef WITH_SYMPHONET
#include "datebook.xpm"
#include "address.xpm"
#include "todo.xpm"
#include "memo.xpm"
#else
#include "datebook_ncc.xpm"
#include "address_ncc.xpm"
#include "todo_ncc.xpm"
#include "memo_ncc.xpm"
#endif


/*#define SHADOW GTK_SHADOW_IN */
/*#define SHADOW GTK_SHADOW_OUT */
/*#define SHADOW GTK_SHADOW_ETCHED_IN */
#define SHADOW GTK_SHADOW_ETCHED_OUT

#define OUTPUT_MINIMIZE 383
#define OUTPUT_RESIZE   384
#define OUTPUT_SETSIZE  385
#define OUTPUT_CLEAR    386

#define MASK_WIDTH  0x08
#define MASK_HEIGHT 0x04
#define MASK_X      0x02
#define MASK_Y      0x01

#define USAGE_STRING _("\njpilot [ [-v] || [-h] || [-d] || [-a] || [-A]\n"\
" -v displays version and exits.\n"\
" -h displays help and exits.\n"\
" -d displays debug info to stdout.\n"\
" -p do not load plugins.\n"\
" -a ignore missed alarms since the last time this program was run.\n"\
" -A ignore all alarms, past and future.\n"\
" The PILOTPORT, and PILOTRATE env variables are used to specify which\n"\
" port to sync on, and at what speed.\n"\
" If PILOTPORT is not set then it defaults to /dev/pilot.\n")


GtkWidget *g_hbox, *g_vbox0;
GtkWidget *g_hbox2, *g_vbox0_1;

GtkWidget *glob_date_label;
GtkTooltips *glob_tooltips;
gint glob_date_timer_tag;
pid_t glob_child_pid;
GtkWidget *g_output_text;
GtkWidget *window;
static GtkWidget *output_pane;
int glob_app = 0;
int glob_focus = 1;
GtkWidget *glob_dialog=NULL;
unsigned char skip_plugins;
				       
int pipe_in, pipe_out;

GtkWidget *sync_window = NULL;

static void delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);

/*
 * Parses the -geometry command line parameter
 */
void geometry_error(const char *str)
{
   /* The log window hasn't been created yet */
   jpilot_logf(LOG_STDOUT, "invalid geometry specification: \"%s\"\n", str);
}

/* gtk_init must already have been called, or will seg fault */
gboolean parse_geometry(const char *str, 
			int window_width, int window_height,
			int *w, int *h, int *x, int *y,
			int *mask)
{
   const char *P;
   int field;
   int *param;
   int negative;
   int max_value;
   int sub_value;

   jpilot_logf(LOG_DEBUG, "parse_geometry()\n");

   if (!(x && y && w && h && str && mask)) {
      return FALSE;
   }

   *x = *y = *w = *h = *mask = 0;
   max_value = sub_value = 0;
   param=w;
   /* what param are we working on x=1, y=2, w=3, h=4 */
   field=1;
   /* 1 for positive, -1 for negative */
   negative=1;

   for (P=str; *P; P++) {
      if (isdigit(*P)) {
	 *mask=(*mask) | 1 << ((4-field) & 0x0F);
	 if (negative==1) {
	    *param = (*param) * 10 + (*P) - '0';
	 }
	 if (negative==-1) {
	    sub_value = sub_value * 10 + (*P) - '0';
	    *param = max_value - sub_value;
	 }
      }
      if ((*P=='x')||(*P=='X')) {
	 field++;
	 if (field==2) {
	    param=h;
	    negative=1;
	 } else {
	    geometry_error(str);
	    return FALSE;
	 }
      }
      if ((*P == '+') || (*P == '-')) {
	 field++;
	 if (field<3) {
	    field=3;
	 }
	 if (field>4) {
	    geometry_error(str);
	    return FALSE;
	 }
      }
      if (*P == '+') {
	 negative=1;
	 if (field==3) {
	    param=x;
	 }
	 if (field==4) {
	    param=y;
	 }
	 *param=0;
      }
      if (*P == '-') {
	 negative=-1;
	 if (field==3) {
	    param=x;
	    if (*mask & MASK_WIDTH) {
	       *param = max_value = gdk_screen_width() - *w;
	    } else {
	       *param = max_value = gdk_screen_width() - window_width;
	    }
	    sub_value=0;
	 }
	 if (field==4) {
	    param=y;
	    if (*mask & MASK_HEIGHT) {
	       *param = max_value = gdk_screen_height() - *h;
	    } else {
	       *param = max_value = gdk_screen_height() - window_height;
	    }
	    sub_value=0;
	 }
      }
   }
   jpilot_logf(LOG_DEBUG, "w=%d, h=%d, x=%d, y=%d, mask=0x%x\n",
	       *w, *h, *x, *y, *mask);
   return TRUE;
}

static void cb_focus(GtkWidget *widget, GdkEvent *event, gpointer data)
{
   int i;
   
   i = GPOINTER_TO_INT(data);
   if (i==0) {
      glob_focus=0;
   }
   if (i==1) {
      glob_focus=1;
      if (GTK_IS_WIDGET(glob_dialog)) {
	 gdk_window_raise(glob_dialog->window);
      }
   }
}

int create_main_boxes()
{
   g_hbox2 = gtk_hbox_new(FALSE, 0);
   g_vbox0_1 = gtk_vbox_new(FALSE, 0);

   gtk_box_pack_start(GTK_BOX(g_hbox), g_hbox2, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(g_vbox0), g_vbox0_1, FALSE, FALSE, 0);
   return 0;
}

int gui_cleanup()
{
#ifdef ENABLE_PLUGINS
   struct plugin_s *plugin;
   GList *plugin_list, *temp_list;
#endif   
   
#ifdef ENABLE_PLUGINS
   plugin_list = NULL;
   plugin_list = get_plugin_list();

   /* Find out which (if any) plugin to call a gui_cleanup on */
   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (plugin->number == glob_app) {
	    if (plugin->plugin_gui_cleanup) {
	       plugin->plugin_gui_cleanup();
	    }
	    break;
	 }
      }
   }
#endif   
   
   switch(glob_app) {
    case DATEBOOK:
      datebook_gui_cleanup();
      break;
    case ADDRESS:
      address_gui_cleanup();
      break;
    case TODO:
      todo_gui_cleanup();
      break;
    case MEMO:
      memo_gui_cleanup();
      break;
    default:
      break;
   }
   return 0;
}

#ifdef ENABLE_PLUGINS
void call_plugin_gui(int number, int unique_id)
{
   struct plugin_s *plugin;
   GList *plugin_list, *temp_list;
   
   if (!number) {
      return;
   }
   
   gui_cleanup();

   plugin_list = NULL;
   plugin_list = get_plugin_list();

   /* destroy main boxes and recreate them */
   gtk_widget_destroy(g_vbox0_1);
   gtk_widget_destroy(g_hbox2);
   create_main_boxes();
   if (glob_date_timer_tag) {
      gtk_timeout_remove(glob_date_timer_tag);
   }

   /* Find out which plugin we are calling */

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (plugin->number == number) {
	    glob_app = plugin->number;
	    if (plugin->plugin_gui) {
	       plugin->plugin_gui(g_vbox0_1, g_hbox2, unique_id);
	    }
	    break;
	 }
      }
   }
}

void cb_plugin_gui(GtkWidget *widget, int number)
{
   call_plugin_gui(number, 0);
}
      
#endif

#ifdef ENABLE_PLUGINS
void call_plugin_help(int number)
{
   struct plugin_s *plugin;
   GList *plugin_list, *temp_list;
   char *button_text[]={"OK"
   };
   char *text;
   char temp[256];
   int width, height;

   if (!number) {
      return;
   }
   
   plugin_list = NULL;
   plugin_list = get_plugin_list();

   /* Find out which plugin we are calling */

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (plugin->number == number) {
	    glob_app = plugin->number;
	    if (plugin->plugin_help) {
	       text = NULL;
	       plugin->plugin_help(&text, &width, &height);
	       if (text) {
		  strncpy(temp, _("OK"), 256);
		  temp[255]='\0';
		  button_text[0]=temp;
		  dialog_generic(GTK_WIDGET(window)->window,
				width, height,
				 _("Help"), plugin->name, text, 1, button_text);
		  free(text);
	       }
	    }
	    break;
	 }
      }
   }
}

void cb_plugin_help(GtkWidget *widget, int number)
{
   call_plugin_help(number);
}

#endif

void cb_print(GtkWidget *widget, gpointer data)
{
   struct plugin_s *plugin;
   GList *plugin_list, *temp_list;
   char *button_text[]={gettext_noop("OK")};
   char temp[256];
   
   switch(glob_app) {
    case DATEBOOK:
      if (print_gui(window) == DIALOG_SAID_PRINT) {
	 datebook_print();
      }
      return;
    case ADDRESS:
      if (print_gui(window) == DIALOG_SAID_PRINT) {
	 address_print();
      }
      return;
    case TODO:
      if (print_gui(window) == DIALOG_SAID_PRINT) {
	 todo_print();
      }
      return;
    case MEMO:
      if (print_gui(window) == DIALOG_SAID_PRINT) {
	 memo_print();
      }
      return;
   }
#ifdef ENABLE_PLUGINS
   plugin_list = NULL;
   plugin_list = get_plugin_list();

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (glob_app == plugin->number) {
	    if (plugin->plugin_print) {
	       plugin->plugin_print();
	       return;
	    }
	 }
      }
   }
#endif
   strncpy(temp, _("OK"), 256);
   temp[255]='\0';
   button_text[0]=temp;
   dialog_generic(GTK_WIDGET(window)->window, 0, 0,
		  _("Print"), "", 
		  _("There is no print support for this conduit."),
		  1, button_text);
}

static void cb_private(GtkWidget *widget, gpointer data)
{
   int privates, was_privates;
   char ascii_password[64];
   int ret;

   was_privates = privates = show_privates(GET_PRIVATES, NULL);

   if (privates==SHOW_PRIVATES) {
      privates = show_privates(HIDE_PRIVATES, NULL);
   } else {
      ret = dialog_password(ascii_password);
      if (ret==1) {
	 privates = show_privates(SHOW_PRIVATES, ascii_password);
      }
   }

   if (was_privates!=privates) {
      cb_app_button(NULL, GINT_TO_POINTER(REDRAW));
   }
}
   
void cb_app_button(GtkWidget *widget, gpointer data)
{
   int app;
   
   app = GPOINTER_TO_INT(data);
   
   if (app==REDRAW) {
      gui_cleanup();
      if (glob_date_timer_tag) {
	 gtk_timeout_remove(glob_date_timer_tag);
      }
      gtk_widget_destroy(g_vbox0_1);
      gtk_widget_destroy(g_hbox2);
      create_main_boxes();
      app = glob_app;
      glob_app = 0;
   }
   
   switch(app) {
    case DATEBOOK:
      if (glob_app == DATEBOOK) {
	 /*refresh screen */
	 datebook_refresh(FALSE);
      } else {
/*	 gtk_container_remove(GTK_CONTAINER(g_vbox0_1->parent), */
/*			      GTK_WIDGET(g_vbox0_1)); */
/*	 gtk_container_remove(GTK_CONTAINER(g_hbox2->parent), */
/*			      GTK_WIDGET(g_hbox2)); */
	 gui_cleanup();
	 if (glob_date_timer_tag) {
	    gtk_timeout_remove(glob_date_timer_tag);
	 }
	 gtk_widget_destroy(g_vbox0_1);
	 gtk_widget_destroy(g_hbox2);
	 create_main_boxes();
	 glob_app = DATEBOOK;
	 datebook_gui(g_vbox0_1, g_hbox2);
      }
      break;
    case ADDRESS:
      if (glob_app == ADDRESS) {
	 /*refresh screen */
	 address_refresh();
      } else {
/*	 gtk_container_remove(GTK_CONTAINER(g_vbox0_1->parent), */
/*			      GTK_WIDGET(g_vbox0_1)); */
/*	 gtk_container_remove(GTK_CONTAINER(g_hbox2->parent), */
/*			      GTK_WIDGET(g_hbox2)); */
	 gui_cleanup();
	 if (glob_date_timer_tag) {
	    gtk_timeout_remove(glob_date_timer_tag);
	 }
	 gtk_widget_destroy(g_vbox0_1);
	 gtk_widget_destroy(g_hbox2);
	 create_main_boxes();
	 glob_app = ADDRESS;
	 address_gui(g_vbox0_1, g_hbox2);
      }
      break;
    case TODO:
      if (glob_app == TODO) {
	 /*refresh screen */
	 todo_refresh();
      } else {
	 gui_cleanup();
	 if (glob_date_timer_tag) {
	    gtk_timeout_remove(glob_date_timer_tag);
	 }
	 gtk_widget_destroy(g_vbox0_1);
	 gtk_widget_destroy(g_hbox2);
	 create_main_boxes();
	 glob_app = TODO;
	 todo_gui(g_vbox0_1, g_hbox2);
      }
      break;
    case MEMO:
      if (glob_app == MEMO) {
	 /*refresh screen */
	 memo_refresh();
      } else {
/*	 gtk_container_remove(GTK_CONTAINER(g_vbox0_1->parent), */
/*			      GTK_WIDGET(g_vbox0_1)); */
/*	 gtk_container_remove(GTK_CONTAINER(g_hbox2->parent), */
/*			      GTK_WIDGET(g_hbox2)); */
	 gui_cleanup();
	 if (glob_date_timer_tag) {
	    gtk_timeout_remove(glob_date_timer_tag);
	 }
	 gtk_widget_destroy(g_vbox0_1);
	 gtk_widget_destroy(g_hbox2);
	 create_main_boxes();
	 glob_app = MEMO;
	 memo_gui(g_vbox0_1, g_hbox2);
      }
      break;
    default:
      /*recursion */
      if ((glob_app==DATEBOOK) ||
	  (glob_app==ADDRESS) ||
	  (glob_app==TODO) ||
	  (glob_app==MEMO) )
	cb_app_button(NULL, GINT_TO_POINTER(glob_app));
      break;
   }
}

void cb_sync_hide(GtkWidget *widget, gpointer data)
{
   gtk_widget_destroy(sync_window);
   
   sync_window=NULL;
}

/*
 * This is called when the user name from the palm doesn't match
 * or the user ID from the palm is 0
 */
void bad_sync_exit_status(int exit_status)
{
   int result;
   char text1[] =
     /*-------------------------------------------*/
     "This palm doesn't have the same user name\n"
     "or user ID as the one that was synced the\n"
     "last time.  Syncing could have unwanted\n"
     "effects.\n"
     "Read the user manual if you are uncertain.";
   char text2[] =
     /*-------------------------------------------*/
     "This palm has a NULL user id.\n"
     "It may have been hard reset.\n"
     "J-Pilot will not restore a palm yet.\n"
     "Use pilot-xfer to restore the palm and\n"
     "install-user to add a username and user ID\n"
     "to the palm.\n"
     "Read the user manual if you are uncertain.";
   char *button_text[]={"OK", "Sync Anyway"
   };
   
   if (!GTK_IS_WINDOW(window)) {
      return;
   }
   if ((exit_status == SYNC_ERROR_NOT_SAME_USERID) ||
       (exit_status == SYNC_ERROR_NOT_SAME_USER)) {
      result = dialog_generic(GTK_WIDGET(window)->window,
			      0, 0,
			      "Sync Problem", "Sync", text1, 2, button_text);
      if (result == DIALOG_SAID_2) {
	 cb_sync(NULL, SYNC_OVERRIDE_USER | (skip_plugins ? SYNC_NO_PLUGINS : 0));
      }
   }
   if (exit_status == SYNC_ERROR_NULL_USERID) {
      dialog_generic(GTK_WIDGET(window)->window,
		     0, 0,
		     "Sync Problem", "Sync", text2, 1, button_text);
   }
}


void cb_read_pipe(gpointer data,
		  gint in,
		  GdkInputCondition condition)
{
   int num;
   char buf[1024];
   int buf_len;
   fd_set fds;
   struct timeval tv;
   int ret;
   char *Pstr1, *Pstr2, *Pstr3;
   int user_len;
   int password_len;
   unsigned long user_id;
   unsigned long ivalue;
   int w, h, new_y;
   int exit_status;
   char user[MAX_PREF_VALUE];
   char password[MAX_PREF_VALUE];

   while(1) {
      /*Linux modifies tv in the select call */
      tv.tv_sec=0;
      tv.tv_usec=0;
      FD_ZERO(&fds);
      FD_SET(in, &fds);
      ret=select(in+1, &fds, NULL, NULL, &tv);
      if (ret<1) break;
      if (!FD_ISSET(in, &fds)) break;
      buf[0]='\0';
      buf_len = read(in, buf, 1022);
      if (buf_len >= 1022) {
	 buf[1022] = '\0';
      } else {
	 if (buf_len > 0) {
	    buf[buf_len]='\0';
	 }
      }
      /*Look for the username */
      Pstr1 = strstr(buf, "sername is");
      if (Pstr1) {
	 Pstr2 = strchr(Pstr1, '\"');
	 if (Pstr2) {
	    Pstr2++;
	    Pstr3 = strchr(Pstr2, '\"');
	    if (Pstr3) {
	       user_len = Pstr3 - Pstr2;
	       if (user_len > MAX_PREF_VALUE) {
		  user_len = MAX_PREF_VALUE;
	       }
	       strncpy(user, Pstr2, user_len);
	       user[user_len] = '\0';
	       jpilot_logf(LOG_DEBUG, "pipe_read: user = %s\n", user);
	       set_pref_char(PREF_USER, user);
	    }
	 }
      }
#ifdef ENABLE_PRIVATE
      /*Look for the Password */
      Pstr1 = strstr(buf, "User Password is");
      if (Pstr1) {
	 Pstr2 = strchr(Pstr1, '\"');
	 if (Pstr2) {
	    Pstr2++;
	    Pstr3 = strchr(Pstr2, '\"');
	    if (Pstr3) {
	       password_len = Pstr3 - Pstr2;
	       if (password_len > MAX_PREF_VALUE) {
		  password_len = MAX_PREF_VALUE;
	       }
	       strncpy(password, Pstr2, password_len);
	       /* Remove this line from the output */
	       buf_len = buf_len - (Pstr3 - Pstr1) - 1;
	       memmove(Pstr1, Pstr3+1, buf_len);
	       password[password_len] = '\0';
	       jpilot_logf(LOG_DEBUG, "pipe_read: password = %s\n", password);
	       set_pref_char(PREF_PASSWORD, password);
	    }
	 }
      }
#endif
      /*Look for the user ID */
      Pstr1 = strstr(buf, "ser ID is");
      if (Pstr1) {
	 Pstr2 = Pstr1 + 9;
	 num = sscanf(Pstr2, "%ld", &user_id);
	 if (num > 0) {
	    jpilot_logf(LOG_DEBUG, "pipe_read: user id = %ld\n", user_id);
	    set_pref(PREF_USER_ID, user_id);
	 } else {
	    jpilot_logf(LOG_DEBUG, "pipe_read: trouble reading user id\n");
	 }
      }
      /*Look for the exit status */
      Pstr1 = strstr(buf, "exiting with status");
      if (Pstr1) {
	 Pstr2 = Pstr1 + 19;
	 num = sscanf(Pstr2, "%d", &exit_status);
	 if (num > 0) {
	    jpilot_logf(LOG_DEBUG, "pipe_read: exit status = %d\n", exit_status);
	 } else {
	    jpilot_logf(LOG_DEBUG, "pipe_read: trouble reading exit status\n");
	 }
	 if ((exit_status == SYNC_ERROR_NOT_SAME_USERID) ||
	     (exit_status == SYNC_ERROR_NOT_SAME_USER)) {
	    bad_sync_exit_status(exit_status);
	 }
	 if (exit_status == SYNC_ERROR_NULL_USERID) {
	    bad_sync_exit_status(exit_status);
	 }
      }
      /* Output the text to the Sync window */
      if (buf_len>0) {
	 gtk_text_insert(GTK_TEXT(g_output_text), NULL, NULL, NULL, buf, buf_len);
	 get_pref(PREF_OUTPUT_HEIGHT, &ivalue, NULL);
	 /* Make them look at least something if output happens */
	 if (ivalue < 60) ivalue=60;
	 gdk_window_get_size(window->window, &w, &h);
	 new_y = h - ivalue;
	 gtk_paned_set_position(GTK_PANED(output_pane), new_y + 2);
      }
      /*Look for finish message */
      Pstr1 = strstr(buf, "Finished");
      if (Pstr1) {
        cb_app_button(NULL, GINT_TO_POINTER(REDRAW));
      }
   }
}

void cb_about(GtkWidget *widget, gpointer data)
{
   char text[255];
   char *button_text[1];
   char temp[256];
   char about[256];

   sprintf(text,
	   /*-------------------------------------------*/
	   _("%s was written by\n"
	     "Judd Montgomery (c) 1999-2000.\n"
	     "judd@jpilot.org\n"
	     "http://jpilot.org\n"),   
	   PN);
   g_snprintf(about, 250, _("About %s"), PN);

   if (GTK_IS_WINDOW(window)) {
      strncpy(temp, _("OK"), 256);
      temp[255]='\0';
      button_text[0]=temp;
      dialog_generic(GTK_WIDGET(window)->window,
 		     0, 0,
		     about, "oOo", text, 1, button_text);
   }
}

void get_main_menu(GtkWidget  *window,
		   GtkWidget **menubar,
		   GList *plugin_list)
/* Some of this code was copied from the gtk_tut.txt file */
#ifndef WITH_SYMPHONET
#define NUM_FACTORY_ITEMS 19
#else
#define NUM_FACTORY_ITEMS 17
#endif
{
  GtkItemFactoryEntry menu_items1[NUM_FACTORY_ITEMS]={
  { NULL, NULL,         NULL,           0,        "<Branch>" },
  { NULL, NULL,         NULL,           0,        "<Tearoff>" },
  { NULL, "<control>F", cb_search_gui,  0,        NULL },
  { NULL, NULL,         NULL,           0,        "<Separator>" },
  { NULL, "<control>I", cb_install_gui, 0,        NULL },
  { NULL, "<control>E", cb_prefs_gui,   GPOINTER_TO_INT(window),   NULL },
  { NULL, "<control>P", cb_print,       0,        NULL },
#ifndef WITH_SYMPHONET
  { NULL, NULL,         NULL,           0,        "<Separator>" },
  { NULL, "<control>Q", delete_event,   0,        NULL },
#endif
  { NULL, NULL,         NULL,           0,        "<Branch>" },
  { NULL, "<control>Z", cb_private,     0,        NULL },
  { NULL, "F1",         cb_app_button,  DATEBOOK, NULL },
  { NULL, "F2",         cb_app_button,  ADDRESS,  NULL },
  { NULL, "F3",         cb_app_button,  TODO,     NULL },
  { NULL, "F4",         cb_app_button,  MEMO,     NULL },
  { NULL, NULL,         NULL,           0,        "<Branch>" },
  { NULL, NULL,         NULL,           0,        "<LastBranch>" },
  { NULL, NULL,         cb_about,       0,        NULL },
  { "END",NULL,         NULL,           0,        NULL }
 };

   GtkItemFactory *item_factory;
   GtkAccelGroup *accel_group;
   gint nmenu_items;
   GtkItemFactoryEntry *menu_items2;
   int i1, i2, i;
   char temp_str[255];

#ifdef ENABLE_PLUGINS
   int count, help_count;
   struct plugin_s *p;
   int str_i;
   char **plugin_menu_strings;
   char **plugin_help_strings;
   GList *temp_list;
   char *F_KEYS[]={"F5","F6","F7","F8","F9","F10","F11","F12"};
   int f_key_count;
#endif

   i=0;
   menu_items1[i++].path=strdup(_("/File"));
   menu_items1[i++].path=strdup(_("/File/tear"));
   menu_items1[i++].path=strdup(_("/File/_Find"));
   menu_items1[i++].path=strdup(_("/File/sep1"));
   menu_items1[i++].path=strdup(_("/File/_Install"));
   menu_items1[i++].path=strdup(_("/File/Preferences"));
   menu_items1[i++].path=strdup(_("/File/_Print"));
#ifndef WITH_SYMPHONET
   menu_items1[i++].path=strdup(_("/File/sep1"));
   menu_items1[i++].path=strdup(_("/File/Quit"));
#endif
   menu_items1[i++].path=strdup(_("/_View"));
   menu_items1[i++].path=strdup(_("/View/Hide-Show Private Records"));
   menu_items1[i++].path=strdup(_("/View/Datebook"));
   menu_items1[i++].path=strdup(_("/View/Addresses"));
   menu_items1[i++].path=strdup(_("/View/Todos"));
   menu_items1[i++].path=strdup(_("/View/Memos"));
   menu_items1[i++].path=strdup(_("/Plugins"));
   menu_items1[i++].path=strdup(_("/_Help"));
   g_snprintf(temp_str, 100, _("/_Help/%s"), PN);
   temp_str[100]='\0';
   menu_items1[i++].path=strdup(temp_str);
   
#ifdef ENABLE_PLUGINS
   /* Go to first entry in the list */
   for (temp_list = plugin_list; temp_list; temp_list = temp_list->prev)
      plugin_list = temp_list;
   
   /* Count the plugin/ entries */
   for (count=0, temp_list = plugin_list;
	temp_list; temp_list = temp_list->next) {
      p = (struct plugin_s *)temp_list->data;
      if (p->menu_name) {
	 count++;
      }
   }
   
   /* Count the help/ entries */
   for (help_count=0, temp_list = plugin_list;
	temp_list; temp_list = temp_list->next) {
      p = (struct plugin_s *)temp_list->data;
      if (p->help_name) {
	 help_count++;
      }
   }
   
   plugin_menu_strings = plugin_help_strings = NULL;
   if (count != 0) {
     plugin_menu_strings = malloc(count * sizeof(char *));
   }
   if (help_count != 0) {
      plugin_help_strings = malloc(help_count * sizeof(char *));
   }
   
   /* Create plugin menu strings */
   str_i = 0;
   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      p = (struct plugin_s *)temp_list->data;
      if (p->menu_name) {
	 g_snprintf(temp_str, 60, _("/Plugins/%s"), p->menu_name);
	 plugin_menu_strings[str_i++]=strdup(temp_str);
      }
   }


   /* Create help menu strings */
   str_i = 0;
   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      p = (struct plugin_s *)temp_list->data;
      if (p->help_name) {
	 g_snprintf(temp_str, 60, _("/_Help/%s"), p->help_name);
	 plugin_help_strings[str_i++]=strdup(temp_str);
      }
   }
#endif

   nmenu_items = (sizeof (menu_items1) / sizeof (menu_items1[0])) - 2;

#ifdef ENABLE_PLUGINS
   if (count) {
      nmenu_items = nmenu_items + count + 1;
   }
   nmenu_items = nmenu_items + help_count;
#endif
   

   menu_items2=malloc(nmenu_items * sizeof(GtkItemFactoryEntry));
   if (!menu_items2) {
      jpilot_logf(LOG_WARN, "get_main_menu(): Out of memory\n");
      return;
   }
   /* Copy the first part of the array until Plugins */
   for (i1=i2=0; ; i1++, i2++) {
      if (!strcmp(menu_items1[i1].path, _("/Plugins"))) {
	 break;
      }
      menu_items2[i2]=menu_items1[i1];
   }

#ifdef ENABLE_PLUGINS
   if (count) {
      /* This is the /Plugins entry */
      menu_items2[i2]=menu_items1[i1];
      i1++; i2++;
      str_i=0;
      for (temp_list = plugin_list, f_key_count=0;
	   temp_list;
	   temp_list = temp_list->next, f_key_count++) {
	 p = (struct plugin_s *)temp_list->data;
	 if (!p->menu_name) {
	    continue;
	 }
	 menu_items2[i2].path=plugin_menu_strings[str_i];
	 if (f_key_count < 8) {
	    menu_items2[i2].accelerator=F_KEYS[f_key_count];
	 } else {
	    menu_items2[i2].accelerator=NULL;
	 }
	 menu_items2[i2].callback=cb_plugin_gui;
	 menu_items2[i2].callback_action=p->number;
	 menu_items2[i2].item_type=0;
	 str_i++;
	 i2++;
      }
   } else {
      /* Skip the /Plugins entry */
      i1++;
   }
#else
   /* Skip the /Plugins entry */
   i1++;
#endif
   
   /* Copy the last part of the array until END */
   for (; ; i1++, i2++) {
      if (!strcmp(menu_items1[i1].path, "END")) {
	 break;
      }
      menu_items2[i2]=menu_items1[i1];
   }

#ifdef ENABLE_PLUGINS
   if (help_count) {
      str_i=0;
      for (temp_list = plugin_list;
	   temp_list;
	   temp_list = temp_list->next) {
	 p = (struct plugin_s *)temp_list->data;
	 if (!p->help_name) {
	    continue;
	 }
	 menu_items2[i2].path=plugin_help_strings[str_i];
	 menu_items2[i2].accelerator=NULL;
	 menu_items2[i2].callback=cb_plugin_help;
	 menu_items2[i2].callback_action=p->number;
	 menu_items2[i2].item_type=0;
	 str_i++;
	 i2++;
      }
   }
#endif

   accel_group = gtk_accel_group_new();

   /* This function initializes the item factory.
    Param 1: The type of menu - can be GTK_TYPE_MENU_BAR, GTK_TYPE_MENU,
    or GTK_TYPE_OPTION_MENU.
    Param 2: The path of the menu.
    Param 3: A pointer to a gtk_accel_group.  The item factory sets up
    the accelerator table while generating menus.
    */
   item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>",
				       accel_group);

   /* This function generates the menu items. Pass the item factory,
    the number of items in the array, the array itself, and any
    callback data for the the menu items. */
   gtk_item_factory_create_items(item_factory, nmenu_items, menu_items2, NULL);
   
   /* Attach the new accelerator group to the window. */
   gtk_accel_group_attach(accel_group, GTK_OBJECT (window));

   if (menubar)
     /* Finally, return the actual menu bar created by the item factory. */
     *menubar = gtk_item_factory_get_widget (item_factory, "<main>");
   
   free(menu_items2);

   /* NUM_FACTORY_ITEMS is just a safety, the loop stops at END */
   for (i=0; i<NUM_FACTORY_ITEMS; i++) {
      if (!strcmp(menu_items1[i].path, "END")) {
	 break;
      }
      free(menu_items1[i].path);
   }

#ifdef ENABLE_PLUGINS
   if (count) {
      for (str_i=0; str_i < count; str_i++) {
	 free(plugin_menu_strings[str_i]);
      }
      free(plugin_menu_strings);
   }
#endif
}

static void delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
   int pw, ph;
   int x,y;
#ifdef ENABLE_PLUGINS
   struct plugin_s *plugin;
   GList *plugin_list, *temp_list;
#endif

   /* gdk_window_get_deskrelative_origin(window->window, &x, &y); */
   gdk_window_get_origin(window->window, &x, &y);
   jpilot_logf(LOG_DEBUG, "x=%d, y=%d\n", x, y);
   
   gdk_window_get_size(window->window, &pw, &ph);
   set_pref(PREF_WINDOW_WIDTH, pw);
   set_pref(PREF_WINDOW_HEIGHT, ph);
   
   set_pref(PREF_LAST_APP, glob_app);
   
   gui_cleanup();

#ifdef ENABLE_PLUGINS
   plugin_list = get_plugin_list();

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (plugin->plugin_exit_cleanup) {
	    jpilot_logf(LOG_DEBUG, "calling plugin_exit_cleanup\n");
	    plugin->plugin_exit_cleanup();
	 }
      }
   }
#endif
   
   if (glob_child_pid) {
      jpilot_logf(LOG_DEBUG, "killing %d\n", glob_child_pid);
	 kill(glob_child_pid, SIGTERM);
   }
   write_rc_file();  /*jpilot.rc */

   cleanup_pc_files();

   gtk_main_quit();
}

void cb_output(GtkWidget *widget, gpointer data)
{
   int flags;
   int w, h, output_height, pane_y;
   long ivalue;
   
   flags=GPOINTER_TO_INT(data);

   if ((flags==OUTPUT_MINIMIZE) || (flags==OUTPUT_RESIZE)) {
      jpilot_logf(LOG_DEBUG,"paned pos = %d\n", GTK_PANED(output_pane)->handle_ypos);
      gdk_window_get_size(window->window, &w, &h);
      output_height = h - GTK_PANED(output_pane)->handle_ypos;
      set_pref(PREF_OUTPUT_HEIGHT, output_height);
      if (flags==OUTPUT_MINIMIZE) {
	 gtk_paned_set_position(GTK_PANED(output_pane), h + 2);
      }
      jpilot_logf(LOG_DEBUG,"output height = %d\n", output_height);
   }
   if (flags==OUTPUT_SETSIZE) {
      get_pref(PREF_OUTPUT_HEIGHT, &ivalue, NULL);
      gdk_window_get_size(window->window, &w, &h);
      pane_y = h - ivalue;
      gtk_paned_set_position(GTK_PANED(output_pane), pane_y + 2);
      jpilot_logf(LOG_DEBUG, "setting output_pane to %d\n", pane_y);
   }
   if (flags==OUTPUT_CLEAR) {
      gtk_text_set_point(GTK_TEXT(g_output_text),
			 gtk_text_get_length(GTK_TEXT(g_output_text)));
      gtk_text_backward_delete(GTK_TEXT(g_output_text),
			       gtk_text_get_length(GTK_TEXT(g_output_text)));
   }
}

static gint cb_output_idle(gpointer data)
{
   cb_output(NULL, data);
   /* returning false removes this handler from being called again */
   return FALSE;
}

static gint cb_output2(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
   /* Because the pane isn't redrawn yet we can get positions from it.
    * So we have to call back after everything is drawn */
   gtk_idle_add(cb_output_idle, data);
   return 0;
}

#ifdef FONT_TEST
void SetFontRecursively(GtkWidget *widget, gpointer data)
{
   GtkStyle *style;
   GdkFont *f;
   
   f = (GdkFont *)data;

   style = gtk_widget_get_style(widget);
   style->font=f;
   gtk_widget_set_style(widget, style);
   if (GTK_IS_CONTAINER(widget)) {
      gtk_container_foreach(GTK_CONTAINER(widget), SetFontRecursively, f);
   }
}
#endif

int main(int   argc,
	 char *argv[])
{
   GtkWidget *main_vbox;
   GtkWidget *temp_hbox;
   GtkWidget *temp_vbox;
   GtkWidget *button_datebook,*button_address,*button_todo,*button_memo;
   GtkWidget *button;
   GtkWidget *arrow;
   GtkWidget *separator;
   GtkStyle *style;
   GdkBitmap *mask;
   GtkWidget *pixmapwid;
   GdkPixmap *pixmap;
   GtkWidget *menubar;
   GtkWidget *vscrollbar;
   unsigned char skip_past_alarms;
   unsigned char skip_all_alarms;
   int filedesc[2];
   long ivalue;
   const char *svalue;
   int sync_only;
   int i, x, y, h, w;
   int bit_mask;
   char title[MAX_PREF_VALUE+256];
   long pref_width, pref_height;
   long char_set;
   char *geometry_str=NULL;
#ifdef ENABLE_PLUGINS
   GList *plugin_list;
   GList *temp_list;
   struct plugin_s *plugin;
   jp_startup_info info;
#endif
#ifdef FONT_TEST
   GdkFont *f;
#endif   

   sync_only=FALSE;
   skip_plugins=FALSE;
   skip_past_alarms=FALSE;
   skip_all_alarms=FALSE;
   /*log all output to a file */
   glob_log_file_mask = LOG_INFO | LOG_WARN | LOG_FATAL | LOG_STDOUT;
   glob_log_stdout_mask = LOG_INFO | LOG_WARN | LOG_FATAL | LOG_STDOUT;
   glob_log_gui_mask = LOG_FATAL | LOG_WARN | LOG_GUI;
   glob_find_id = 0;

   read_rc_file();  /*jpilot.rc */

   w = h = x = y = bit_mask = 0;

   get_pref(PREF_WINDOW_WIDTH, &pref_width, NULL);
   get_pref(PREF_WINDOW_HEIGHT, &pref_height, NULL);

   for (i=1; i<argc; i++) {
      if (!strncasecmp(argv[i], "-v", 2)) {
	 printf("%s\n", VERSION_STRING);
	 exit(0);
      }
      if ( (!strncasecmp(argv[i], "-h", 2)) || 
	  (!strncasecmp(argv[i], "-?", 2)) ) {
	 printf("%s\n", USAGE_STRING);
	 exit(0);
      }
      if (!strncasecmp(argv[i], "-d", 2)) {
	 glob_log_stdout_mask = 0xFFFF;
	 glob_log_file_mask = 0xFFFF;
	 jpilot_logf(LOG_DEBUG, "Debug messages on.\n");
      }
      if (!strncasecmp(argv[i], "-p", 2)) {
	 skip_plugins = TRUE;
	 jpilot_logf(LOG_INFO, "Not loading plugins.\n");
      }
      if (!strncasecmp(argv[i], "-A", 2)) {
	 skip_all_alarms = TRUE;
	 jpilot_logf(LOG_INFO, "Ignoring all alarms.\n");
      }
      if (!strncasecmp(argv[i], "-a", 2)) {
	 skip_past_alarms = TRUE;
	 jpilot_logf(LOG_INFO, "Ignoring past alarms.\n");
      }
      if (!strncasecmp(argv[i], "-geometry", 9)) {
	 /* The '=' isn't specified in `man X`, but we will be nice */
	 if (argv[i][9]=='=') {
	    geometry_str=argv[i]+9;
	 } else {
	    if (i<argc) {
	       geometry_str=argv[i+1];
	    }
	 }
      }
   }

   /*Check to see if ~/.jpilot is there, or create it */
   jpilot_logf(LOG_DEBUG, "calling check_hidden_dir\n");
   if (check_hidden_dir()) {
      exit(1);
   }

   /*Check to see if DB files are there */
   /*If not copy some empty ones over */
   check_copy_DBs_to_home();

#ifdef ENABLE_PLUGINS
   plugin_list=NULL;
   if (!skip_plugins) {
      load_plugins();
   }
   plugin_list = get_plugin_list();

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      jpilot_logf(LOG_DEBUG, "plugin: [%s] was loaded\n", plugin->name);
   }

   
   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (plugin->plugin_startup) {
	    info.base_dir = strdup(BASE_DIR);
	    jpilot_logf(LOG_DEBUG, "calling plugin_startup for [%s]\n", plugin->name);
	    plugin->plugin_startup(&info);
	    if (info.base_dir) {
	       free(info.base_dir);
	    }
	 }
      }
   }
#endif

   glob_date_timer_tag=0;
   glob_child_pid=0;

   /*Create a pipe to send the sync output, etc. */
   if (pipe(filedesc) <0) {
      jpilot_logf(LOG_FATAL, "Could not open pipe\n");
      exit(-1);
   }
   pipe_in = filedesc[0];
   pipe_out = filedesc[1];
   
   gtk_set_locale();

#if defined(ENABLE_NLS)
#ifdef HAVE_LOCALE_H
   setlocale(LC_ALL, "");
#endif
   bindtextdomain("jpilot", LOCALEDIR);
   textdomain("jpilot");
#endif

   get_pref(PREF_CHAR_SET, &char_set, NULL);
   switch (char_set) {
    case CHAR_SET_JAPANESE:
      gtk_rc_parse("gtkrc.ja");
      break;
    case CHAR_SET_TRADITIONAL_CHINESE:
      gtk_rc_parse("gtkrc.zh_TW.Big5");
      break;
    case CHAR_SET_KOREAN:
      gtk_rc_parse("gtkrc.ko");
      break;
      /* Since Now, these are not supported yet. */
#if 0
    case CHAR_SET_SIMPLIFIED_CHINESE:
      gtk_rc_parse("gtkrc.zh_CN");
      break;
    case CHAR_SET_1250:
      gtk_rc_parse("gtkrc.???");
      break;
    case CHAR_SET_1251:
      gtk_rc_parse("gtkrc.iso-8859-5");
      break;
    case CHAR_SET_1251_B:
      gtk_rc_parse("gtkrc.ru");
      break;
#endif
    default:
      /* do nothing */
   }

   gtk_init(&argc, &argv);

   read_gtkrc_file();
   
   /* gtk_init must already have been called, or will seg fault */
   parse_geometry(geometry_str, pref_width, pref_height,
		  &w, &h, &x, &y, &bit_mask);

   get_pref(PREF_USER, &ivalue, &svalue);
   
   strcpy(title, PN" "VERSION);
   if ((svalue) && (svalue[0])) {
      strcat(title, " User: ");
      strcat(title, svalue);
   }

   if ((bit_mask & MASK_X) || (bit_mask & MASK_Y)) {
      window = gtk_widget_new(GTK_TYPE_WINDOW,
			      "type", GTK_WINDOW_TOPLEVEL,
			      "x", x, "y", y,
			      "title", title,
			      NULL);
   } else {
      window = gtk_widget_new(GTK_TYPE_WINDOW,
			      "type", GTK_WINDOW_TOPLEVEL,
			      "title", title,
			      NULL);
   }
   if (bit_mask & MASK_WIDTH) {
      pref_width = w;
   }
   if (bit_mask & MASK_HEIGHT) {
      pref_height = h;
   }

   gtk_window_set_default_size(GTK_WINDOW(window), pref_width, pref_height);

   gtk_signal_connect(GTK_OBJECT(window), "focus_in_event",
		      GTK_SIGNAL_FUNC(cb_focus), GINT_TO_POINTER(1));

   gtk_signal_connect(GTK_OBJECT(window), "focus_out_event",
		      GTK_SIGNAL_FUNC(cb_focus), GINT_TO_POINTER(0));

   /* Set a handler for delete_event that immediately */
   /* exits GTK. */
   gtk_signal_connect(GTK_OBJECT(window), "delete_event",
		      GTK_SIGNAL_FUNC(delete_event), NULL);

   gtk_container_set_border_width(GTK_CONTAINER(window), 0);

   main_vbox = gtk_vbox_new(FALSE, 0);
   g_hbox = gtk_hbox_new(FALSE, 0);
   g_vbox0 = gtk_vbox_new(FALSE, 0);

   /* Output Pane */
   output_pane = gtk_vpaned_new();

   gtk_signal_connect(GTK_OBJECT(output_pane), "button_release_event",
		      GTK_SIGNAL_FUNC(cb_output2),
		      GINT_TO_POINTER(OUTPUT_RESIZE));

   gtk_container_add(GTK_CONTAINER(window), output_pane);

   gtk_paned_pack1(GTK_PANED(output_pane), main_vbox, FALSE, FALSE);

   /* Create the Menu Bar at the top */
#ifdef ENABLE_PLUGINS
   get_main_menu(window, &menubar, plugin_list);
#else
   get_main_menu(window, &menubar, NULL);
#endif
   gtk_box_pack_start(GTK_BOX(main_vbox), menubar, FALSE, FALSE, 0);
   gtk_menu_bar_set_shadow_type(GTK_MENU_BAR(menubar), GTK_SHADOW_NONE);

   
   gtk_box_pack_start(GTK_BOX(main_vbox), g_hbox, TRUE, TRUE, 3);
   gtk_container_set_border_width(GTK_CONTAINER(g_hbox), 10);
   gtk_box_pack_start(GTK_BOX(g_hbox), g_vbox0, FALSE, FALSE, 3);
   

   /* Make the output text window */
   temp_hbox = gtk_hbox_new(FALSE, 0);
   gtk_widget_set_usize(GTK_WIDGET(temp_hbox), 1, 1);
   gtk_container_set_border_width(GTK_CONTAINER(temp_hbox), 5);
   gtk_paned_pack2(GTK_PANED(output_pane), temp_hbox, FALSE, FALSE);

   temp_vbox = gtk_vbox_new(FALSE, 0);
   gtk_box_pack_end(GTK_BOX(temp_hbox), temp_vbox, FALSE, FALSE, 0);

   g_output_text = gtk_text_new(NULL, NULL);
   gtk_text_set_editable(GTK_TEXT(g_output_text), FALSE);
   gtk_text_set_word_wrap(GTK_TEXT(g_output_text), TRUE);
   vscrollbar = gtk_vscrollbar_new(GTK_TEXT(g_output_text)->vadj);
   gtk_box_pack_start(GTK_BOX(temp_hbox), g_output_text, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(temp_hbox), vscrollbar, FALSE, FALSE, 0);

   /* button = gtk_button_new_with_label(_("Minimize")); */
   button = gtk_button_new();
   arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_OUT);
   gtk_container_add(GTK_CONTAINER(button), arrow);
   gtk_box_pack_start(GTK_BOX(temp_vbox), button, TRUE, TRUE, 3);
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_output),
		      GINT_TO_POINTER(OUTPUT_MINIMIZE));

   button = gtk_button_new_with_label(_("Clear"));
   gtk_box_pack_start(GTK_BOX(temp_vbox), button, TRUE, TRUE, 3);
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_output),
		      GINT_TO_POINTER(OUTPUT_CLEAR));
   /* End output text */

   /* Create "Datebook" button */
   temp_hbox = gtk_hbox_new(FALSE, 0);
   button_datebook = gtk_button_new();
   gtk_signal_connect(GTK_OBJECT(button_datebook), "clicked",
		      GTK_SIGNAL_FUNC(cb_app_button), GINT_TO_POINTER(DATEBOOK));
   gtk_widget_set_usize(button_datebook, 46, 46);
   gtk_box_pack_start(GTK_BOX(g_vbox0), temp_hbox, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(temp_hbox), button_datebook, FALSE, FALSE, 0);
   
   /* Create "Address" button */
   temp_hbox = gtk_hbox_new(FALSE, 0);
   button_address = gtk_button_new();
   gtk_signal_connect(GTK_OBJECT(button_address), "clicked",
		      GTK_SIGNAL_FUNC(cb_app_button), GINT_TO_POINTER(ADDRESS));
   gtk_widget_set_usize(button_address, 46, 46);
   gtk_box_pack_start(GTK_BOX(g_vbox0), temp_hbox, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(temp_hbox), button_address, FALSE, FALSE, 0);

   /* Create "Todo" button */
   temp_hbox = gtk_hbox_new(FALSE, 0);
   button_todo = gtk_button_new();
   gtk_signal_connect(GTK_OBJECT(button_todo), "clicked",
		      GTK_SIGNAL_FUNC(cb_app_button), GINT_TO_POINTER(TODO));
   gtk_widget_set_usize(button_todo, 46, 46);
   gtk_box_pack_start(GTK_BOX(g_vbox0), temp_hbox, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(temp_hbox), button_todo, FALSE, FALSE, 0);

   /* Create "memo" button */
   temp_hbox = gtk_hbox_new(FALSE, 0);
   button_memo = gtk_button_new();
   gtk_signal_connect(GTK_OBJECT(button_memo), "clicked",
		      GTK_SIGNAL_FUNC(cb_app_button), GINT_TO_POINTER(MEMO));
   gtk_widget_set_usize(button_memo, 46, 46);
   gtk_box_pack_start(GTK_BOX(g_vbox0), temp_hbox, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(temp_hbox), button_memo, FALSE, FALSE, 0);

   gtk_widget_set_name(button_datebook, "button_app");
   gtk_widget_set_name(button_address, "button_app");
   gtk_widget_set_name(button_todo, "button_app");
   gtk_widget_set_name(button_memo, "button_app");

   separator = gtk_hseparator_new();
   gtk_box_pack_start(GTK_BOX(g_vbox0), separator, FALSE, TRUE, 5);

   /* Create tooltips */
   glob_tooltips = gtk_tooltips_new();
   
#ifndef WITH_SYMPHONET
   /* Create "Quit" button */
   button = gtk_button_new_with_label(_("Quit!"));
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(delete_event), NULL);
   gtk_box_pack_start(GTK_BOX(g_vbox0), button, FALSE, FALSE, 0);
#endif

   /* Create "Sync" button */
   button = gtk_button_new_with_label(_("Sync"));
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_sync),
		      GINT_TO_POINTER(skip_plugins ? SYNC_NO_PLUGINS : 0));

   gtk_box_pack_start(GTK_BOX(g_vbox0), button, FALSE, FALSE, 0);

   gtk_tooltips_set_tip(glob_tooltips, button, _("Sync your palm to the desktop"), NULL);


   /* Create "Backup" button in left column */
   button = gtk_button_new_with_label(_("Backup"));
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_sync),
		      GINT_TO_POINTER
		      (skip_plugins ? SYNC_NO_PLUGINS | SYNC_FULL_BACKUP
		      : SYNC_FULL_BACKUP));
   gtk_box_pack_start(GTK_BOX(g_vbox0), button, FALSE, FALSE, 0);

   gtk_tooltips_set_tip(glob_tooltips, button, _("Sync your palm to the desktop\n"
			"and then do a backup"), NULL);

   /*Separator */
   separator = gtk_hseparator_new();
   gtk_box_pack_start(GTK_BOX(g_vbox0), separator, FALSE, TRUE, 5);

   /*This creates the 2 main boxes that are changeable */
   create_main_boxes();

   gtk_widget_show_all(window);
   
   gtk_widget_show(window);
   
   style = gtk_widget_get_style(window);

   /* Create "Datebook" pixmap */
   pixmap = gdk_pixmap_create_from_xpm_d(window->window, &mask,
					 &style->bg[GTK_STATE_NORMAL],
					 datebook_xpm);
   pixmapwid = gtk_pixmap_new(pixmap, mask);
   gtk_widget_show(pixmapwid);
   gtk_container_add(GTK_CONTAINER(button_datebook), pixmapwid);

   /* Create "Address" pixmap */
   pixmap = gdk_pixmap_create_from_xpm_d(window->window, &mask,
					 &style->bg[GTK_STATE_NORMAL],
					 address_xpm);
   pixmapwid = gtk_pixmap_new(pixmap, mask);
   gtk_widget_show(pixmapwid);
   gtk_container_add(GTK_CONTAINER(button_address), pixmapwid);

   /* Create "Todo" pixmap */
   pixmap = gdk_pixmap_create_from_xpm_d(window->window, &mask,
					 &style->bg[GTK_STATE_NORMAL],
					 todo_xpm);
   pixmapwid = gtk_pixmap_new(pixmap, mask);
   gtk_widget_show(pixmapwid);
   gtk_container_add(GTK_CONTAINER(button_todo), pixmapwid);

   /* Create "memo" pixmap */
   pixmap = gdk_pixmap_create_from_xpm_d(window->window, &mask,
					 &style->bg[GTK_STATE_NORMAL],
					 memo_xpm);
   pixmapwid = gtk_pixmap_new(pixmap, mask);
   gtk_widget_show(pixmapwid);
   gtk_container_add(GTK_CONTAINER(button_memo), pixmapwid);

   gtk_tooltips_set_tip(glob_tooltips, button_datebook, _("Datebook/Go to Today"), NULL);

   gtk_tooltips_set_tip(glob_tooltips, button_address, _("Address Book"), NULL);

   gtk_tooltips_set_tip(glob_tooltips, button_todo, _("ToDo List"), NULL);

   gtk_tooltips_set_tip(glob_tooltips, button_memo, _("Memo Pad"), NULL);

   /*Set a callback for our pipe from the sync child process */
   gdk_input_add(pipe_in, GDK_INPUT_READ, cb_read_pipe, window);

   get_pref(PREF_LAST_APP, &ivalue, NULL);
   /* We don't want to start up to a plugin because the plugin might
    * repeatedly segfault.  Of course main apps can do that, but since I
    * handle the email support...
    */
   if ((ivalue==ADDRESS) ||
       (ivalue==DATEBOOK) ||
       (ivalue==TODO) ||
       (ivalue==MEMO)) {
      cb_app_button(NULL, GINT_TO_POINTER(ivalue));
   }

   /* Set the pane size */
   gdk_window_get_size(window->window, &w, &h);
   gtk_paned_set_position(GTK_PANED(output_pane), h + 2);

   /* ToDo this is broken, it doesn't take into account the window
    * decorations.  I can't find a GDK call that does */
   gdk_window_get_origin(window->window, &x, &y);
   jpilot_logf(LOG_DEBUG, "x=%d, y=%d\n", x, y);

   gdk_window_get_size(window->window, &w, &h);
   jpilot_logf(LOG_DEBUG, "w=%d, h=%d\n", w, h);

#ifdef FONT_TEST
   f=gdk_fontset_load("-adobe-utopia-medium-r-normal-*-*-200-*-*-p-*-iso8859-1");
   SetFontRecursively(window, f);
#endif
#ifdef ALARMS
   alarms_init(skip_past_alarms, skip_all_alarms);
#endif

   gtk_main();

   return 0;
}
