/* weekview_gui.c
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
#include <gtk/gtk.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "utils.h"
#include "prefs.h"
#include "log.h"
#include "datebook.h"
#include <pi-datebook.h>
#include "config.h"

static GtkWidget *window=NULL;
static GtkWidget *glob_week_texts[8];
static struct tm glob_week_date;

/* Function prototypes */
int clear_weeks_appts(GtkWidget **day_texts);
int display_weeks_appts(struct tm *date_in, GtkWidget **day_texts);


static gboolean cb_destroy(GtkWidget *widget)
{
   window = NULL;
   return FALSE;
}

static void
  cb_quit(GtkWidget *widget,
	   gpointer   data)
{
   window = NULL;
   gtk_widget_destroy(data);
}

void freeze_weeks_appts()
{
   int i;
   
   for (i=0; i<8; i++) {
      gtk_text_freeze(GTK_TEXT(glob_week_texts[i]));
   }
}

void thaw_weeks_appts()
{
   int i;
   
   for (i=0; i<8; i++) {
      gtk_text_thaw(GTK_TEXT(glob_week_texts[i]));
   }
}

static void
cb_week_move(GtkWidget *widget,
	     gpointer   data)
{
   if (GPOINTER_TO_INT(data)==-1) {
      sub_days_from_date(&glob_week_date, 7);
   }
   if (GPOINTER_TO_INT(data)==1) {
      add_days_to_date(&glob_week_date, 7);
   }
   freeze_weeks_appts();
   clear_weeks_appts(glob_week_texts);
   display_weeks_appts(&glob_week_date, glob_week_texts);
   thaw_weeks_appts();
}

int clear_weeks_appts(GtkWidget **day_texts)
{
   int i;
   
   for (i=0; i<8; i++) {
      gtk_text_set_point(GTK_TEXT(day_texts[i]), 0);
      gtk_text_forward_delete(GTK_TEXT(day_texts[i]),
			      gtk_text_get_length(GTK_TEXT(day_texts[i])));
   }
   return 0;
}

/*
 * This function requires that date_in be the date of the first day of
 * the week (be it a Sunday, or a Monday).
 * It will then print the next eight days to the day_texts array of
 * text boxes.
 */
int display_weeks_appts(struct tm *date_in, GtkWidget **day_texts)
{
   char *days[]={"Sunday","Monday","Tuesday","Wednesday","Thurday",
      "Friday","Saturday"};
   AppointmentList *a_list;
   AppointmentList *temp_al;
   struct tm date;
   GtkWidget **text;
   char desc[256];
   int n, i;
   long ivalue;
   const char *svalue;
   char str[82];
   long fdow;
   char short_date[32];
   char default_date[]="%x";
   GdkFont *small_font;
   GdkColor color;
   GdkColormap *colormap;

   a_list = NULL;
   text = day_texts;

   small_font = gdk_fontset_load("-misc-fixed-medium-r-*-*-*-100-*-*-*-*-*");
   
   color.red = 0xAAAA;
   color.green = 0xAAAA;
   color.blue = 0xAAAA;

   colormap = gtk_widget_get_colormap(text[0]);
   gdk_color_alloc(colormap, &color);

   memcpy(&date, date_in, sizeof(struct tm));

   get_pref(PREF_FDOW, &fdow, &svalue);

   get_pref(PREF_SHORTDATE, &ivalue, &svalue);
   if (svalue==NULL) {
      svalue = default_date;
   }
   
   for (i=0; i<8; i++, add_days_to_date(&date, 1)) {
      strftime(short_date, 30, svalue, &date);
      g_snprintf(str, 80, "%s %s\n", days[(i + fdow)%7], short_date);
      str[80]='\0';
      gtk_text_insert(GTK_TEXT(glob_week_texts[i]), NULL, NULL, &color, str, -1);
   }

   /* Get all of the appointments */
   get_days_appointments(&a_list, NULL);

   /* iterate through eight days */
   memcpy(&date, date_in, sizeof(struct tm));

   for (n=0; n<8; n++, add_days_to_date(&date, 1)) {
      for (temp_al = a_list; temp_al; temp_al=temp_al->next) {
	 if (isApptOnDate(&(temp_al->ma.a), &date)) {
	    if (temp_al->ma.a.event) {
	       desc[0]='\0';
	    } else {
	       strftime(desc, 20, "%l:%M%P ", &(temp_al->ma.a.begin));
	    }
	    if (temp_al->ma.a.description) {
	       strncat(desc, temp_al->ma.a.description, 70);
	       desc[62]='\0';
	    }
	    remove_cf_lfs(desc);
	    strcat(desc, "\n");
	    gtk_text_insert(GTK_TEXT(text[n]),
			    small_font, NULL, NULL, desc, -1);
	 }
      }
   }
   free_AppointmentList(&a_list);
   
   return 0;
}

void weekview_gui(struct tm *date_in)
{
   GtkWidget *button;
   GtkWidget *arrow;
   GtkWidget *align;
   GtkWidget *vbox, *hbox;
   GtkWidget *hbox_temp;
   GtkWidget *vbox_left, *vbox_right;
   const char *str_fdow;
   long fdow;
   int i;
   
   if (GTK_IS_WIDGET(window)) {
      return;
   }

   memcpy(&glob_week_date, date_in, sizeof(struct tm));

   window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
   gtk_container_set_border_width(GTK_CONTAINER(window), 10);
   gtk_window_set_title(GTK_WINDOW(window), PN" Weekly View");

   gtk_signal_connect(GTK_OBJECT(window), "destroy",
                      GTK_SIGNAL_FUNC(cb_destroy), window);

   vbox = gtk_vbox_new(FALSE, 0);
   gtk_container_add(GTK_CONTAINER(window), vbox);

   /* This box has the close button and arrows in it */
   align = gtk_alignment_new(0.5, 0.5, 0, 0);
   gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 0);

   hbox_temp = gtk_hbox_new(FALSE, 0);

   gtk_container_add(GTK_CONTAINER(align), hbox_temp);

   /*Make a left arrow for going back a week */
   button = gtk_button_new();
   arrow = gtk_arrow_new(GTK_ARROW_LEFT, GTK_SHADOW_OUT);
   gtk_container_add(GTK_CONTAINER(button), arrow);
   gtk_signal_connect(GTK_OBJECT(button), "clicked", 
		      GTK_SIGNAL_FUNC(cb_week_move),
		      GINT_TO_POINTER(-1));
   gtk_box_pack_start(GTK_BOX(hbox_temp), button, FALSE, FALSE, 3);

   /* Create a "Quit" button */
   button = gtk_button_new_with_label("Close");
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_quit), window);
   gtk_box_pack_start(GTK_BOX(hbox_temp), button, FALSE, FALSE, 0);

   /*Make a right arrow for going forward a week */
   button = gtk_button_new();
   arrow = gtk_arrow_new(GTK_ARROW_RIGHT, GTK_SHADOW_OUT);
   gtk_container_add(GTK_CONTAINER(button), arrow);
   gtk_signal_connect(GTK_OBJECT(button), "clicked", 
		      GTK_SIGNAL_FUNC(cb_week_move),
		      GINT_TO_POINTER(1));
   gtk_box_pack_start(GTK_BOX(hbox_temp), button, FALSE, FALSE, 3);

   get_pref(PREF_FDOW, &fdow, &str_fdow);

   hbox = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

   vbox_left = gtk_vbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), vbox_left, TRUE, TRUE, 0);

   vbox_right = gtk_vbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), vbox_right, TRUE, TRUE, 0);

   /* Get the first day of the week */
   sub_days_from_date(&glob_week_date, (7 - fdow + glob_week_date.tm_wday)%7);

   for (i=0; i<8; i++) {
      glob_week_texts[i] = gtk_text_new(NULL, NULL);
      if (i%2) {
	 gtk_box_pack_start(GTK_BOX(vbox_right), glob_week_texts[i], FALSE, FALSE, 0);
      } else {
	 gtk_box_pack_start(GTK_BOX(vbox_left), glob_week_texts[i], FALSE, FALSE, 0);
      }
   }

   display_weeks_appts(&glob_week_date, glob_week_texts);

   gtk_widget_show_all(window);
}
