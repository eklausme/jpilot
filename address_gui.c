/* address_gui.c
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
#include <gdk/gdkkeysyms.h>
#include <gdk/gdk.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "address.h"
#include "log.h"
#include "prefs.h"
#include "print.h"
#include "password.h"
#include <pi-dlp.h>


/*#define SHADOW GTK_SHADOW_IN */
/*#define SHADOW GTK_SHADOW_OUT */
/*#define SHADOW GTK_SHADOW_ETCHED_IN */
#define SHADOW GTK_SHADOW_ETCHED_OUT

#define CONNECT_SIGNALS 400
#define DISCONNECT_SIGNALS 401
   
#define NUM_MENU_ITEM1 8
#define NUM_MENU_ITEM2 8
#define NUM_ADDRESS_CAT_ITEMS 16
#define NUM_ADDRESS_ENTRIES 19
#define NUM_ADDRESS_LABELS 22
#define NUM_PHONE_ENTRIES 5

GtkWidget *clist;
GtkWidget *address_text[22];
GtkWidget *text;
GtkWidget *vscrollbar;
static GtkWidget *private_checkbox;
GtkWidget *phone_list_menu[NUM_PHONE_ENTRIES];
GtkWidget *menu;
GtkWidget *menu_item[NUM_MENU_ITEM1][NUM_MENU_ITEM2];
GtkWidget *address_cat_menu2;
/*We need an extra one for the ALL category */
GtkWidget *address_cat_menu_item1[NUM_ADDRESS_CAT_ITEMS+1];
GtkWidget *address_cat_menu_item2[NUM_ADDRESS_CAT_ITEMS];
static GtkWidget *category_menu1;
static GtkWidget *scrolled_window;
GtkWidget *address_quickfind_entry;
static GtkWidget *notebook;
static GtkWidget *pane;
static GtkWidget *radio_button[NUM_PHONE_ENTRIES];

static struct AddressAppInfo address_app_info;
static struct sorted_cats sort_l[NUM_ADDRESS_CAT_ITEMS];
int address_category=CATEGORY_ALL;
int address_phone_label_selected[NUM_PHONE_ENTRIES];
static int clist_row_selected;
extern GtkTooltips *glob_tooltips;

static GtkWidget *new_record_button;
static GtkWidget *apply_record_button;
static GtkWidget *add_record_button;
static int record_changed;

static void connect_changed_signals(int con_or_dis);
void update_address_screen();
int address_clist_redraw();
static int address_find();

static void init()
{
   int i, j;
   record_changed=CLEAR_FLAG;
   for (i=0; i<NUM_MENU_ITEM1; i++) {
      for (j=0; j<NUM_MENU_ITEM2; j++) {
	 menu_item[i][j] = NULL;
      }
   }
   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      address_cat_menu_item2[i] = NULL;
   }
}
   
static void
set_new_button_to(int new_state)
{
   jpilot_logf(LOG_DEBUG, "set_new_button_to new %d old %d\n", new_state, record_changed);

   if (record_changed==new_state) {
      return;
   }

   switch (new_state) {
    case MODIFY_FLAG:
      gtk_widget_show(apply_record_button);
      break;
    case NEW_FLAG:
      gtk_widget_show(add_record_button);
      break;
    case CLEAR_FLAG:
      gtk_widget_show(new_record_button);
      break;
    default:
      return;
   }
   switch (record_changed) {
    case MODIFY_FLAG:
      gtk_widget_hide(apply_record_button);
      break;
    case NEW_FLAG:
      gtk_widget_hide(add_record_button);
      break;
    case CLEAR_FLAG:
      gtk_widget_hide(new_record_button);
      break;
   }
   record_changed=new_state;
}

static void
cb_record_changed(GtkWidget *widget,
		  gpointer   data)
{
   jpilot_logf(LOG_DEBUG, "cb_record_changed\n");
   if (record_changed==CLEAR_FLAG) {
      connect_changed_signals(DISCONNECT_SIGNALS);
      set_new_button_to(MODIFY_FLAG);
   }
}

static void connect_changed_signals(int con_or_dis)
{
   int i, j;
   static int connected=0;

   /* CONNECT */
   if ((con_or_dis==CONNECT_SIGNALS) && (!connected)) {
      connected=1;

      for (i=0; i<NUM_MENU_ITEM1; i++) {
	 for (j=0; j<NUM_MENU_ITEM2; j++) {
	    if (menu_item[i][j]) {
	       gtk_signal_connect(GTK_OBJECT(menu_item[i][j]), "toggled",
				  GTK_SIGNAL_FUNC(cb_record_changed), NULL);
	    }
	 }
      }
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 if (radio_button[i]) {
	    gtk_signal_connect(GTK_OBJECT(radio_button[i]), "toggled",
			       GTK_SIGNAL_FUNC(cb_record_changed), NULL);
	 }
      }
      for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
	 if (address_cat_menu_item2[i]) {
	    gtk_signal_connect(GTK_OBJECT(address_cat_menu_item2[i]), "toggled",
			       GTK_SIGNAL_FUNC(cb_record_changed), NULL);
	 }
      }
      for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
	 gtk_signal_connect(GTK_OBJECT(address_text[i]), "changed",
			    GTK_SIGNAL_FUNC(cb_record_changed), NULL);
      }
      gtk_signal_connect(GTK_OBJECT(private_checkbox), "toggled",
			 GTK_SIGNAL_FUNC(cb_record_changed), NULL);
   }

   /* DISCONNECT */
   if ((con_or_dis==DISCONNECT_SIGNALS) && (connected)) {
      connected=0;
      for (i=0; i<NUM_MENU_ITEM1; i++) {
	 for (j=0; j<NUM_MENU_ITEM2; j++) {
	    if (menu_item[i][j]) {
	       gtk_signal_disconnect_by_func(GTK_OBJECT(menu_item[i][j]),
					     GTK_SIGNAL_FUNC(cb_record_changed), NULL);     
	    }
	 }
      }
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 if (radio_button[i]) {
	    gtk_signal_disconnect_by_func(GTK_OBJECT(radio_button[i]),
					  GTK_SIGNAL_FUNC(cb_record_changed), NULL);
	 }
      }
      for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
	 if (address_cat_menu_item2[i]) {
	    gtk_signal_disconnect_by_func(GTK_OBJECT(address_cat_menu_item2[i]),
					  GTK_SIGNAL_FUNC(cb_record_changed), NULL);     
	 }
      }
      for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
	 gtk_signal_disconnect_by_func(GTK_OBJECT(address_text[i]),
				       GTK_SIGNAL_FUNC(cb_record_changed), NULL);     
      }
      gtk_signal_disconnect_by_func(GTK_OBJECT(private_checkbox),
				    GTK_SIGNAL_FUNC(cb_record_changed), NULL);
   }
}

int address_print()
{
   long this_many;
   MyAddress *ma;
   AddressList *address_list;
   AddressList address_list1;

   get_pref(PREF_PRINT_THIS_MANY, &this_many, NULL);

   address_list=NULL;
   if (this_many==1) {
      ma = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
      if (ma < (MyAddress *)CLIST_MIN_DATA) {
	 return -1;
      }
      memcpy(&(address_list1.ma), ma, sizeof(MyAddress));
      address_list1.next=NULL;
      address_list = &address_list1;
   }
   if (this_many==2) {
      get_addresses2(&address_list, SORT_ASCENDING, 2, 2, 2, address_category);
   }
   if (this_many==3) {
      get_addresses2(&address_list, SORT_ASCENDING, 2, 2, 2, CATEGORY_ALL);
   }

   print_addresses(address_list);

   if ((this_many==2) || (this_many==3)) {
      free_AddressList(&address_list);
   }

   return 0;
}

static int find_sorted_cat(int cat)
{
   int i;
   for (i=0; i< NUM_ADDRESS_CAT_ITEMS; i++) {
      if (sort_l[i].cat_num==cat) {
	 return i;
      }
   }
   return 0;
}


void cb_delete_address(GtkWidget *widget,
		       gpointer   data)
{
   MyAddress *ma;
   int flag;

   ma = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
   if (ma < (MyAddress *)CLIST_MIN_DATA) {
      return;
   }
   flag = GPOINTER_TO_INT(data);
   if ((flag==MODIFY_FLAG) || (flag==DELETE_FLAG)) {
      delete_pc_record(ADDRESS, ma, flag);
      if (flag==DELETE_FLAG) {
	 /* when we redraw we want to go to the line above the deleted one */
	 if (clist_row_selected>0) {
	    clist_row_selected--;
	 }
      }
   }
   
   address_clist_redraw();
}

void cb_resort(GtkWidget *widget,
	       gpointer   data)
{
   int by_company;
   
   by_company=GPOINTER_TO_INT(data);

   if (sort_override) {
      sort_override=0;
   } else {
      sort_override=1;
      by_company=!(by_company & 1);
   }

   if (by_company) {
      gtk_clist_set_column_title(GTK_CLIST(clist), 0, _("Company/Name"));
   } else {
      gtk_clist_set_column_title(GTK_CLIST(clist), 0, _("Name/Company"));
   }

   address_clist_redraw();
}


void cb_phone_menu(GtkWidget *item, unsigned int value)
{
   if (!item)
     return;
   if ((GTK_CHECK_MENU_ITEM(item))->active) {
      jpilot_logf(LOG_DEBUG, "phone_menu = %d\n", (value & 0xF0) >> 4);
      jpilot_logf(LOG_DEBUG, "selection = %d\n", value & 0x0F);
      address_phone_label_selected[(value & 0xF0) >> 4] = value & 0x0F;
   }
}

void cb_notebook_changed(GtkWidget *widget,
			 GtkWidget *widget2,
			 int        page,
			 gpointer   data)
{
   int prev_page;

   /* GTK calls this function while it is destroying the notebook
    * I use this function to tell if it is being destroyed */
   prev_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(widget));
   if (prev_page<0) {
      return;
   }
   jpilot_logf(LOG_DEBUG, "cb_notebook_changed(), prev_page=%d, page=%d\n", prev_page, page);
   set_pref(PREF_ADDRESS_NOTEBOOK_PAGE, page);
}

void cb_new_address_done(GtkWidget *widget,
			 gpointer   data)
{
   int i;
   struct Address a;
   MyAddress *ma;
   unsigned char attrib;
   unsigned int unique_id;
   
   bzero(&a, sizeof(a));
   
   if ((GPOINTER_TO_INT(data)==NEW_FLAG) || 
       (GPOINTER_TO_INT(data)==MODIFY_FLAG)) {
      /*These rec_types are both the same for now */
      if (GPOINTER_TO_INT(data)==MODIFY_FLAG) {
	 ma = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
	 if (ma < (MyAddress *)CLIST_MIN_DATA) {
	    return;
	 }
	 if ((ma->rt==DELETED_PALM_REC) || (ma->rt==MODIFIED_PALM_REC)) {
	    jpilot_logf(LOG_INFO, "You can't modify a record that is deleted\n");
	    return;
	 }
      }
      a.showPhone=0;
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 if (GTK_TOGGLE_BUTTON(radio_button[i])->active) {
	    a.showPhone=i;
	 }
      }
      for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
	 a.entry[i] = 
	   gtk_editable_get_chars(GTK_EDITABLE(address_text[i]), 0, -1);
      }
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 a.phoneLabel[i]=address_phone_label_selected[i];
      }

      /*Get the category that is set from the menu */
      attrib = 0;
      for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
	 if (GTK_IS_WIDGET(address_cat_menu_item2[i])) {
	    if (GTK_CHECK_MENU_ITEM(address_cat_menu_item2[i])->active) {
	       attrib = sort_l[i].cat_num;
	       break;
	    }
	 }
      }
      if (GTK_TOGGLE_BUTTON(private_checkbox)->active) {
	 attrib |= dlpRecAttrSecret;
      }

      set_new_button_to(CLEAR_FLAG);

      pc_address_write(&a, NEW_PC_REC, attrib, &unique_id);
      free_Address(&a);
      if (GPOINTER_TO_INT(data) == MODIFY_FLAG) {
	 cb_delete_address(NULL, data);
      } else {
	 address_clist_redraw();
      }
      glob_find_id = unique_id;
      address_find();
   }
}

void clear_details()
{
   int i;
   int new_cat;
   int sorted_position;
   
   /* Need to disconnect these signals first */
   set_new_button_to(NEW_FLAG);
   connect_changed_signals(DISCONNECT_SIGNALS);

   /* Clear the quickview */
   gtk_text_set_point(GTK_TEXT(text), 0);
   gtk_text_forward_delete(GTK_TEXT(text),
			   gtk_text_get_length(GTK_TEXT(text)));

   /*Clear all the address entry texts */
   for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
      gtk_text_set_point(GTK_TEXT(address_text[i]), 0);
      gtk_text_forward_delete(GTK_TEXT(address_text[i]),
			      gtk_text_get_length(GTK_TEXT(address_text[i])));
   }
   for (i=0; i<NUM_PHONE_ENTRIES; i++) {
      if (GTK_IS_WIDGET(menu_item[i][i])) {
	 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
					(menu_item[i][i]), TRUE);
	 gtk_option_menu_set_history(GTK_OPTION_MENU(phone_list_menu[i]), i);
      }
   }
   if (address_category==CATEGORY_ALL) {
      new_cat = 0;
   } else {
      new_cat = address_category;
   }
   sorted_position = find_sorted_cat(new_cat);
   if (sorted_position<0) {
      jpilot_logf(LOG_WARN, "Category is not legal\n");
   } else {
      gtk_check_menu_item_set_active
	(GTK_CHECK_MENU_ITEM(address_cat_menu_item2[sorted_position]), TRUE);
      gtk_option_menu_set_history(GTK_OPTION_MENU(address_cat_menu2), sorted_position);
   }

   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(private_checkbox), FALSE);

   connect_changed_signals(CONNECT_SIGNALS);
}

void cb_address_clear(GtkWidget *widget,
		      gpointer   data)
{
   clear_details();
   gtk_notebook_set_page(GTK_NOTEBOOK(notebook), 0);
   gtk_widget_grab_focus(GTK_WIDGET(address_text[0]));
}

void cb_address_quickfind(GtkWidget *widget,
			  gpointer   data)
{
   char *entry_text;
   int i, r, found, found_at, line_count;
   char *clist_text;
   
   found_at = 0;
   entry_text = gtk_entry_get_text(GTK_ENTRY(widget));
   if (!strlen(entry_text)) {
      return;
   }
   /*100000 is just to prevent ininite looping during a solar flare */
   for (found = i = 0; i<100000; i++) {
      r = gtk_clist_get_text(GTK_CLIST(clist), i, 0, &clist_text);
      if (!r) {
	 break;
      }
      if (found) {
	 continue;
      }
      if (!strncasecmp(clist_text, entry_text, strlen(entry_text))) {
	 found = 1;
	 found_at = i;
	 gtk_clist_select_row(GTK_CLIST(clist), i, 0);
      }
   }
   line_count = i;
	 
   if (found) {
      move_scrolled_window(scrolled_window,
			   ((float)found_at)/((float)line_count));
   }
}

void cb_address_category(GtkWidget *item, int selection)
{
   if (!item)
     return;
   if ((GTK_CHECK_MENU_ITEM(item))->active) {
      address_category = selection;
      jpilot_logf(LOG_DEBUG, "address_category = %d\n",address_category);
      update_address_screen();
   }
}


void cb_address_clist_sel(GtkWidget      *clist,
			  gint           row,
			  gint           column,
			  GdkEventButton *event,
			  gpointer       data)
{
   /*The rename-able phone entries are indexes 3,4,5,6,7 */
   struct Address *a;
   MyAddress *ma;
   int cat, count, sorted_position;
   int i, i2;
   /*This is because the palm doesn't show the address entries in order */
   int order[NUM_ADDRESS_LABELS]={
      0,1,13,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18,19,20,21
   };
   char *clist_text, *entry_text;

   clist_row_selected=row;

   ma = gtk_clist_get_row_data(GTK_CLIST(clist), row);
   if (ma==NULL) {
      return;
   }

   set_new_button_to(CLEAR_FLAG);
   
   connect_changed_signals(DISCONNECT_SIGNALS);
   
   a=&(ma->a);
   clist_text = NULL;
   gtk_clist_get_text(GTK_CLIST(clist), row, 0, &clist_text);
   entry_text = gtk_entry_get_text(GTK_ENTRY(address_quickfind_entry));
   if (strncasecmp(clist_text, entry_text, strlen(entry_text))) {
      gtk_entry_set_text(GTK_ENTRY(address_quickfind_entry), "");
   }

   gtk_text_freeze(GTK_TEXT(text));

   gtk_text_set_point(GTK_TEXT(text), 0);
   gtk_text_forward_delete(GTK_TEXT(text),
			   gtk_text_get_length(GTK_TEXT(text)));

   gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, _("Category: "), -1);
   gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL,
		   address_app_info.category.name[ma->attrib & 0x0F], -1);
   gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, "\n", -1);
   for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
      i2=order[i];
      if (a->entry[i2]) {
	 if (i2>2 && i2<8) {
	    gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL,
			    address_app_info.phoneLabels[a->phoneLabel[i2-3]],
			    -1);
	 } else {
	    gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, address_app_info.labels[i2], -1);
	 }
	 gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, ": ", -1);
	 gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, a->entry[i2], -1);
	 gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, "\n", -1);
      }
   }
   gtk_text_thaw(GTK_TEXT(text));

   cat = ma->attrib & 0x0F;
   sorted_position = find_sorted_cat(cat);
   if (address_cat_menu_item2[sorted_position]==NULL) {
      /* Illegal category, Assume that category 0 is Unfiled and valid*/
      jpilot_logf(LOG_DEBUG, "Category is not legal\n");
      cat = sorted_position = 0;
      sorted_position = find_sorted_cat(cat);
   }
   /* We need to count how many items down in the list this is */
   for (i=sorted_position, count=0; i>=0; i--) {
      if (address_cat_menu_item2[i]) {
	 count++;
      }
   }
   count--;
   
   if (sorted_position<0) {
      jpilot_logf(LOG_WARN, "Category is not legal\n");
   } else {
      if (address_cat_menu_item2[sorted_position]) {
	 gtk_check_menu_item_set_active
	   (GTK_CHECK_MENU_ITEM(address_cat_menu_item2[sorted_position]), TRUE);
      }
      gtk_option_menu_set_history(GTK_OPTION_MENU(address_cat_menu2), count);
   }
   
   for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
      gtk_text_set_point(GTK_TEXT(address_text[i]), 0);
      gtk_text_forward_delete(GTK_TEXT(address_text[i]),
			      gtk_text_get_length(GTK_TEXT(address_text[i])));
      if (a->entry[i]) {
	 gtk_text_insert(GTK_TEXT(address_text[i]), NULL,NULL,NULL, a->entry[i], -1);
      }
   }
   for (i=0; i<NUM_PHONE_ENTRIES; i++) {
      if (GTK_IS_WIDGET(menu_item[i][a->phoneLabel[i]])) {
	 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
					(menu_item[i][a->phoneLabel[i]]), TRUE);
	 gtk_option_menu_set_history(GTK_OPTION_MENU(phone_list_menu[i]),
				     a->phoneLabel[i]);
      }
   }
   if ((a->showPhone > -1) && (a->showPhone < NUM_PHONE_ENTRIES)) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_button[a->showPhone]),
				   TRUE);
   }
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(private_checkbox),
				ma->attrib & dlpRecAttrSecret);

   connect_changed_signals(CONNECT_SIGNALS);
}

void update_address_screen()
{
   int num_entries, entries_shown, i;
   int row_count;
   int show1, show2, show3;
   gchar *empty_line[] = { "","","" };
   GdkPixmap *pixmap_note;
   GdkBitmap *mask_note;
   GdkColor color;
   GdkColormap *colormap;
   AddressList *temp_al;
   static AddressList *address_list=NULL;
   char str[50];
   int by_company;
   int show_priv;

   row_count=((GtkCList *)clist)->rows;
   
   free_AddressList(&address_list);
#ifdef JPILOT_DEBUG
    for (i=0;i<NUM_ADDRESS_CAT_ITEMS;i++) {
      jpilot_logf(LOG_DEBUG, "renamed:[%02d]:\n",address_app_info.category.renamed[i]);
      jpilot_logf(LOG_DEBUG, "category name:[%02d]:",i);
      print_string(address_app_info.category.name[i],16);
      jpilot_logf(LOG_DEBUG, "category ID:%d\n", address_app_info.category.ID[i]);
   }

   for (i=0;i<NUM_ADDRESS_LABELS;i++) {
      jpilot_logf(LOG_DEBUG, "labels[%02d]:",i);
      print_string(address_app_info.labels[i],16);
   }
   for (i=0;i<8;i++) {
      jpilot_logf(LOG_DEBUG, "phoneLabels[%d]:",i);
      print_string(address_app_info.phoneLabels[i],16);
   }
   jpilot_logf(LOG_DEBUG, "country %d\n",address_app_info.country);
   jpilot_logf(LOG_DEBUG, "sortByCompany %d\n",address_app_info.sortByCompany);
#endif

   num_entries = get_addresses2(&address_list, SORT_ASCENDING, 2, 2, 1, CATEGORY_ALL);
   /*gtk_text_backward_delete(GTK_TEXT(text1), */
   /*		    gtk_text_get_length(GTK_TEXT(text1))); */
   
   if (address_list==NULL) {
      gtk_tooltips_set_tip(glob_tooltips, category_menu1, _("0 records"), NULL);   
      return;
   }
   
   /*Clear the text box to make things look nice */
   gtk_text_set_point(GTK_TEXT(text), 0);
   gtk_text_forward_delete(GTK_TEXT(text),
			   gtk_text_get_length(GTK_TEXT(text)));

   gtk_clist_freeze(GTK_CLIST(clist));

   by_company = address_app_info.sortByCompany;
   if (sort_override) {
      by_company=!(by_company & 1);
   }
   if (by_company) {
      show1=2; /*company */
      show2=0; /*last name */
      show3=1; /*first name */
   } else {
      show1=0; /*last name */
      show2=1; /*first name */
      show3=2; /*company */
   }
   

   entries_shown=0;


   show_priv = show_privates(GET_PRIVATES, NULL);
   for (temp_al = address_list, i=0; temp_al; temp_al=temp_al->next) {
      if ((show_priv != SHOW_PRIVATES) && 
	  (temp_al->ma.attrib & dlpRecAttrSecret)) {
	 continue;
      }
      if ( ((temp_al->ma.attrib & 0x0F) != address_category) &&
	  address_category != CATEGORY_ALL) {
	 continue;
      }
      
      str[0]='\0';
      if (temp_al->ma.a.entry[show1] || temp_al->ma.a.entry[show2]) {
	 if (temp_al->ma.a.entry[show1] && temp_al->ma.a.entry[show2]) {
	    g_snprintf(str, 48, "%s, %s", temp_al->ma.a.entry[show1], temp_al->ma.a.entry[show2]);
	 }
	 if (temp_al->ma.a.entry[show1] && ! temp_al->ma.a.entry[show2]) {
	    strncpy(str, temp_al->ma.a.entry[show1], 48);
	 }
	 if (! temp_al->ma.a.entry[show1] && temp_al->ma.a.entry[show2]) {
	    strncpy(str, temp_al->ma.a.entry[show2], 48);
	 }
      } else if (temp_al->ma.a.entry[show3]) {
	    strncpy(str, temp_al->ma.a.entry[show3], 48);
      } else {
	    strcpy(str, "-Unnamed-");
      }
      if (entries_shown+1>row_count) {
	 gtk_clist_append(GTK_CLIST(clist), empty_line);
      }
      gtk_clist_set_text(GTK_CLIST(clist), entries_shown, 0, str);
      gtk_clist_set_text(GTK_CLIST(clist), entries_shown, 1, temp_al->ma.a.entry[temp_al->ma.a.showPhone+3]);
      gtk_clist_set_row_data(GTK_CLIST(clist), entries_shown, &(temp_al->ma));

      switch (temp_al->ma.rt) {
       case NEW_PC_REC:
	 colormap = gtk_widget_get_colormap(clist);
	 color.red=CLIST_NEW_RED;
	 color.green=CLIST_NEW_GREEN;
	 color.blue=CLIST_NEW_BLUE;
	 gdk_color_alloc(colormap, &color);
	 gtk_clist_set_background(GTK_CLIST(clist), entries_shown, &color);
	 break;
       case DELETED_PALM_REC:
	 colormap = gtk_widget_get_colormap(clist);
	 color.red=CLIST_DEL_RED;
	 color.green=CLIST_DEL_GREEN;
	 color.blue=CLIST_DEL_BLUE;
	 gdk_color_alloc(colormap, &color);
	 gtk_clist_set_background(GTK_CLIST(clist), entries_shown, &color);
	 break;
       case MODIFIED_PALM_REC:
	 colormap = gtk_widget_get_colormap(clist);
	 color.red=CLIST_MOD_RED;
	 color.green=CLIST_MOD_GREEN;
	 color.blue=CLIST_MOD_BLUE;
	 gdk_color_alloc(colormap, &color);
	 gtk_clist_set_background(GTK_CLIST(clist), entries_shown, &color);
	 break;
       default:
	 gtk_clist_set_background(GTK_CLIST(clist), entries_shown, NULL);
      }
      
      if (temp_al->ma.a.entry[18]) {
	 /*Put a note pixmap up */
	 get_pixmaps(clist, PIXMAP_NOTE, &pixmap_note, &mask_note);
	 gtk_clist_set_pixmap(GTK_CLIST(clist), entries_shown, 2, pixmap_note, mask_note);
      } else {
	 gtk_clist_set_text(GTK_CLIST(clist), entries_shown, 2, "");
      }

      entries_shown++;
   }
   
   /* If there is an item in the list, select the first one */
   if (entries_shown>0) {
      gtk_clist_select_row(GTK_CLIST(clist), 0, 1);
   }
   
   for (i=row_count-1; i>=entries_shown; i--) {
      gtk_clist_remove(GTK_CLIST(clist), i);
   }

   gtk_clist_thaw(GTK_CLIST(clist));
   
   sprintf(str, _("%d of %d records"), entries_shown, num_entries);
   gtk_tooltips_set_tip(glob_tooltips, category_menu1, str, NULL);   

   set_new_button_to(CLEAR_FLAG);
}

/*default set is which menu item is to be set on by default */
/*set is which set in the menu_item array to use */
static int make_phone_menu(int default_set, unsigned int callback_id, int set)
{
   int i;
   GSList *group;
   
   phone_list_menu[set] = gtk_option_menu_new();
   
   menu = gtk_menu_new();
   group = NULL;
   
   for (i=0; i<8; i++) {
      if (address_app_info.phoneLabels[i][0]) {
	 menu_item[set][i] = gtk_radio_menu_item_new_with_label(
			group, address_app_info.phoneLabels[i]);
	 gtk_signal_connect(GTK_OBJECT(menu_item[set][i]), "activate",
			    cb_phone_menu, GINT_TO_POINTER(callback_id + i));
	 group = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(menu_item[set][i]));
	 gtk_menu_append(GTK_MENU(menu), menu_item[set][i]);
	 gtk_widget_show(menu_item[set][i]);
      }
   }
   /*Set this one to active */
   if (GTK_IS_WIDGET(menu_item[set][default_set])) {
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
				     menu_item[set][default_set]), TRUE);
   }
   
   gtk_option_menu_set_menu(GTK_OPTION_MENU(phone_list_menu[set]), menu);
   /*Make this one show up by default */
   gtk_option_menu_set_history(GTK_OPTION_MENU(phone_list_menu[set]),
			       default_set);
   
   gtk_widget_show(phone_list_menu[set]);
   
   return 0;
}

static int make_category_menu(GtkWidget **category_menu,
			      int include_all)
{
   GtkWidget *menu;
   GSList    *group;
   int i;
   int offset;
   GtkWidget **address_cat_menu_item;

   if (include_all) {
      address_cat_menu_item = address_cat_menu_item1;
   } else {
      address_cat_menu_item = address_cat_menu_item2;
   }

   *category_menu = gtk_option_menu_new();
   
   menu = gtk_menu_new();
   group = NULL;

   offset=0;
   if (include_all) {
      address_cat_menu_item[0] = gtk_radio_menu_item_new_with_label(group, _("All"));
      gtk_signal_connect(GTK_OBJECT(address_cat_menu_item[0]), "activate",
			 cb_address_category, GINT_TO_POINTER(CATEGORY_ALL));
      group = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(address_cat_menu_item[0]));
      gtk_menu_append(GTK_MENU(menu), address_cat_menu_item[0]);
      gtk_widget_show(address_cat_menu_item[0]);
      offset=1;
   }
   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      if (sort_l[i].Pcat[0]) {
	 address_cat_menu_item[i+offset] = gtk_radio_menu_item_new_with_label(
	    group, sort_l[i].Pcat);
	 if (include_all) {
	    gtk_signal_connect(GTK_OBJECT(address_cat_menu_item[i+offset]), "activate",
			       cb_address_category, GINT_TO_POINTER(sort_l[i].cat_num));
	 }
	 group = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(address_cat_menu_item[i+offset]));
	 gtk_menu_append(GTK_MENU(menu), address_cat_menu_item[i+offset]);
	 gtk_widget_show(address_cat_menu_item[i+offset]);
      }
   }

   gtk_option_menu_set_menu(GTK_OPTION_MENU(*category_menu), menu);
   
   return 0;
}

/* returns 1 if found, 0 if not */
static int address_find()
{
   int r, found_at, total_count;

   r = 0;
   if (glob_find_id) {
      r = clist_find_id(clist,
			glob_find_id,
			&found_at,
			&total_count);
      if (r) {
	 /*avoid dividing by zero */
	 if (total_count == 0) {
	    total_count = 1;
	 }
	 gtk_clist_select_row(GTK_CLIST(clist), found_at, 1);
	 if (!gtk_clist_row_is_visible(GTK_CLIST(clist), found_at)) {
	    move_scrolled_window_hack(scrolled_window,
				      (float)found_at/(float)total_count);
	 }
      }
      glob_find_id = 0;
   }
   return r;
}

/* This redraws the clist and goes back to the same line number */
int address_clist_redraw()
{
   int line_num;
   
   line_num = clist_row_selected;

   update_address_screen();
   
   gtk_clist_select_row(GTK_CLIST(clist), line_num, 0);
   
   return 0;
}

int address_refresh()
{
   int index;

   if (glob_find_id) {
      address_category = CATEGORY_ALL;
   }
   if (address_category==CATEGORY_ALL) {
      index=0;
   } else {
      index=find_sorted_cat(address_category)+1;
   }
   update_address_screen();
   if (index<0) {
      jpilot_logf(LOG_WARN, "Category not legal\n");
   } else {
      gtk_option_menu_set_history(GTK_OPTION_MENU(category_menu1), index);
      gtk_check_menu_item_set_active
	(GTK_CHECK_MENU_ITEM(address_cat_menu_item1[index]), TRUE);
   }
   address_find();
   return 0;
}

static gboolean
  cb_key_pressed(GtkWidget *widget, GdkEventKey *event,
		 gpointer next_widget) 
{
   /* This is needed because the text boxes aren't shown on the 
    * screen in the same order as the array.  I show them in the
    * same order that the palm does */
   int page[NUM_ADDRESS_ENTRIES]={
      0,0,0,0,0,0,0,1,1,1,1,1,2,0,2,2,2,2,0
   };
   int i;

   if (event->keyval == GDK_Tab) {
      /* See if they are at the end of the text */
      if (gtk_text_get_point(GTK_TEXT(widget)) ==
	  gtk_text_get_length(GTK_TEXT(widget))) {
	 gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event"); 
	 gtk_widget_grab_focus(GTK_WIDGET(next_widget));
	 /* Change the notebook page */
	 for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
	    if (address_text[i] == widget) {
	       gtk_notebook_set_page(GTK_NOTEBOOK(notebook), page[i]);
	       break;
	    }
	 }
	 return TRUE;
      }
   }
   return FALSE; 
}

int address_gui_cleanup()
{
   connect_changed_signals(DISCONNECT_SIGNALS);
   set_pref(PREF_ADDRESS_PANE, GTK_PANED(pane)->handle_xpos);
   return 0;
}

/*
 * Main function
 */
int address_gui(GtkWidget *vbox, GtkWidget *hbox)
{
   extern GtkWidget *glob_date_label;
   extern int glob_date_timer_tag;
   GtkWidget *vbox1, *vbox2;
   GtkWidget *hbox_temp;
   GtkWidget *vbox_temp1, *vbox_temp2, *vbox_temp3, *hbox_temp4;
   GtkWidget *separator;
   GtkWidget *label;
   GtkWidget *button;
   GtkWidget *frame;
   GtkWidget *table1, *table2, *table3;
   GtkWidget *notebook_tab;
   GSList *group;
   long ivalue, notebook_page;
   const char *svalue;
   char *titles[]={"","",""};
   

   int i, i1, i2;
   int order[NUM_ADDRESS_ENTRIES+1]={
      0,1,13,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18,0
   };

   clist_row_selected=0;
   
   init();

   get_address_app_info(&address_app_info);

   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      sort_l[i].Pcat=address_app_info.category.name[i];
      sort_l[i].cat_num=i;
   }
   qsort(sort_l, NUM_ADDRESS_CAT_ITEMS, sizeof(struct sorted_cats), cat_compare);
#ifdef JPILOT_DEBUG
   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      printf("cat %d [%s]\n", sort_l[i].cat_num, sort_l[i].Pcat);
   }
#endif

   if (address_app_info.category.name[address_category][0]=='\0') {
      address_category=CATEGORY_ALL;
   }

   pane = gtk_hpaned_new();
   get_pref(PREF_ADDRESS_PANE, &ivalue, &svalue);
   gtk_paned_set_position(GTK_PANED(pane), ivalue + 2);
   
   gtk_box_pack_start(GTK_BOX(hbox), pane, TRUE, TRUE, 5);
   
   vbox1 = gtk_vbox_new(FALSE, 0);
   vbox2 = gtk_vbox_new(FALSE, 0);
   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_paned_pack1(GTK_PANED(pane), vbox1, TRUE, FALSE);
   gtk_paned_pack2(GTK_PANED(pane), vbox2, TRUE, FALSE);

   /* Separator */
   separator = gtk_hseparator_new();
   gtk_box_pack_start(GTK_BOX(vbox1), separator, FALSE, FALSE, 5);

   /* Make the Today is: label */
   glob_date_label = gtk_label_new(" ");
   gtk_box_pack_start(GTK_BOX(vbox1), glob_date_label, FALSE, FALSE, 0);
   timeout_date(NULL);
   glob_date_timer_tag = gtk_timeout_add(CLOCK_TICK, timeout_date, NULL);

   /* Separator */
   separator = gtk_hseparator_new();
   gtk_box_pack_start(GTK_BOX(vbox1), separator, FALSE, FALSE, 5);

   /* Put the category menu up */
   make_category_menu(&category_menu1, TRUE);
   gtk_box_pack_start(GTK_BOX(vbox1), category_menu1, FALSE, FALSE, 0);
   
   /* Put the address list window up */
   scrolled_window = gtk_scrolled_window_new(NULL, NULL);
   /*gtk_widget_set_usize(GTK_WIDGET(scrolled_window), 150, 0); */
   gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 0);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_box_pack_start(GTK_BOX(vbox1), scrolled_window, TRUE, TRUE, 0);

   clist = gtk_clist_new_with_titles(3, titles);

   if (address_app_info.sortByCompany) {
      gtk_clist_set_column_title(GTK_CLIST(clist), 0, _("Company/Name"));
   } else {
      gtk_clist_set_column_title(GTK_CLIST(clist), 0, _("Name/Company"));
   }
   gtk_signal_connect(GTK_OBJECT(GTK_CLIST(clist)->column[0].button),
		      "clicked", GTK_SIGNAL_FUNC(cb_resort), 
		      GINT_TO_POINTER(address_app_info.sortByCompany));
   gtk_clist_set_column_title(GTK_CLIST(clist), 1, _("Phone"));
   gtk_clist_set_column_title(GTK_CLIST(clist), 2, _("Note"));

   gtk_signal_connect(GTK_OBJECT(clist), "select_row",
		      GTK_SIGNAL_FUNC(cb_address_clist_sel),
		      text);
   gtk_clist_set_shadow_type(GTK_CLIST(clist), SHADOW);
   gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_BROWSE);
   gtk_clist_set_column_width(GTK_CLIST(clist), 0, 140);
   gtk_clist_set_column_width(GTK_CLIST(clist), 1, 140);
   gtk_clist_set_column_width(GTK_CLIST(clist), 2, 11);
   gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(clist));
   /*gtk_clist_set_column_justification(GTK_CLIST(clist), 1, GTK_JUSTIFY_RIGHT); */
   
   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox1), hbox_temp, FALSE, FALSE, 0);

   label = gtk_label_new(_("Quick Find"));
   gtk_box_pack_start(GTK_BOX(hbox_temp), label, FALSE, FALSE, 0);

   address_quickfind_entry = gtk_entry_new();
   gtk_signal_connect(GTK_OBJECT(address_quickfind_entry), "changed",
		      GTK_SIGNAL_FUNC(cb_address_quickfind),
		      NULL);
   gtk_box_pack_start(GTK_BOX(hbox_temp), address_quickfind_entry, TRUE, TRUE, 0);
   gtk_widget_grab_focus(GTK_WIDGET(address_quickfind_entry));
   

   /* The new entry gui */
   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox2), hbox_temp, FALSE, FALSE, 0);

   /* Delete Button */
   button = gtk_button_new_with_label(_("Delete"));
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_delete_address),
		      GINT_TO_POINTER(DELETE_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), button, TRUE, TRUE, 0);
   
   /* Create "Copy" button */
   button = gtk_button_new_with_label(_("Copy"));
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_new_address_done),
		      GINT_TO_POINTER(NEW_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), button, TRUE, TRUE, 0);

   /* Create "New" button */
   new_record_button = gtk_button_new_with_label(_("New Record"));
   gtk_signal_connect(GTK_OBJECT(new_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_address_clear), NULL);
   gtk_box_pack_start(GTK_BOX(hbox_temp), new_record_button, TRUE, TRUE, 0);

   /* Create "Add Record" button */
   add_record_button = gtk_button_new_with_label(_("Add Record"));
   gtk_signal_connect(GTK_OBJECT(add_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_new_address_done),
		      GINT_TO_POINTER(NEW_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), add_record_button, TRUE, TRUE, 0);
   gtk_widget_set_name(GTK_WIDGET(GTK_LABEL(GTK_BIN(add_record_button)->child)),
		       "label_high");

   /* Create "apply changes" button */
   apply_record_button = gtk_button_new_with_label(_("Apply Changes"));
   gtk_signal_connect(GTK_OBJECT(apply_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_new_address_done),
		      GINT_TO_POINTER(MODIFY_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), apply_record_button, TRUE, TRUE, 0);
   gtk_widget_set_name(GTK_WIDGET(GTK_LABEL(GTK_BIN(apply_record_button)->child)),
		       "label_high");

   /*Separator */
   separator = gtk_hseparator_new();
   gtk_box_pack_start(GTK_BOX(vbox2), separator, FALSE, FALSE, 5);


   /*Private check box */
   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox2), hbox_temp, FALSE, FALSE, 0);
   private_checkbox = gtk_check_button_new_with_label(_("Private"));
   gtk_box_pack_end(GTK_BOX(hbox_temp), private_checkbox, FALSE, FALSE, 0);

   /*Add the new category menu */
   make_category_menu(&address_cat_menu2, FALSE);
   gtk_box_pack_start(GTK_BOX(hbox_temp), address_cat_menu2, TRUE, TRUE, 0);
   
   
   /*Add the notebook for new entries */
   notebook = gtk_notebook_new();
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
   gtk_notebook_popup_enable(GTK_NOTEBOOK(notebook));
   gtk_signal_connect(GTK_OBJECT(notebook), "switch-page",
		      GTK_SIGNAL_FUNC(cb_notebook_changed), NULL);
 
   gtk_box_pack_start(GTK_BOX(vbox2), notebook, TRUE, TRUE, 0);

   /*Page 1 */
   notebook_tab = gtk_label_new(_("Name"));
   vbox_temp1 = gtk_vbox_new(FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_temp1, notebook_tab);
   /* Notebook tabs have to be shown before the show_all */
   gtk_widget_show(vbox_temp1);
   gtk_widget_show(notebook_tab);
   
   /*Page 2 */
   notebook_tab = gtk_label_new(_("Address"));
   vbox_temp2 = gtk_vbox_new(FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_temp2, notebook_tab);
   /* Notebook tabs have to be shown before the show_all */
   gtk_widget_show(vbox_temp2);
   gtk_widget_show(notebook_tab);
   
   /*Page 3 */
   notebook_tab = gtk_label_new(_("Other"));
   vbox_temp3 = gtk_vbox_new(FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_temp3, notebook_tab);
   /* Notebook tabs have to be shown before the show_all */
   gtk_widget_show(vbox_temp3);
   gtk_widget_show(notebook_tab);

   /*Page 3 */
   notebook_tab = gtk_label_new(_("All"));
   hbox_temp4 = gtk_vbox_new(FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), hbox_temp4, notebook_tab);
   /* Notebook tabs have to be shown before the show_all */
   gtk_widget_show(hbox_temp4);
   gtk_widget_show(notebook_tab);

   /*Put a table on every page */
   table1 = gtk_table_new(9, 3, FALSE);
   gtk_box_pack_start(GTK_BOX(vbox_temp1), table1, TRUE, TRUE, 0);
   table2 = gtk_table_new(9, 2, FALSE);
   gtk_box_pack_start(GTK_BOX(vbox_temp2), table2, TRUE, TRUE, 0);
   table3 = gtk_table_new(9, 2, FALSE);
   gtk_box_pack_start(GTK_BOX(vbox_temp3), table3, TRUE, TRUE, 0);

   label = NULL;
   for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
      i2=order[i];
      if (i2>2 && i2<8) {
	 make_phone_menu(i2-3, (i2-3)<<4, i2-3);
      } else {
 	 label = gtk_label_new(address_app_info.labels[i2]);
	 /*gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT); */
	 gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
      }
      address_text[i2] = gtk_text_new(NULL, NULL);
   
      gtk_text_set_editable(GTK_TEXT(address_text[i2]), TRUE);
      gtk_text_set_word_wrap(GTK_TEXT(address_text[i2]), TRUE);
      gtk_widget_set_usize(GTK_WIDGET(address_text[i2]), 0, 25);
      /*gtk_box_pack_start(GTK_BOX(hbox_temp), address_text[i2], TRUE, TRUE, 0); */
      /*hbox_temp = gtk_hbox_new(FALSE, 0); */
      if (i<9) {
	 if (i2>2 && i2<8) {
	    gtk_table_attach_defaults(GTK_TABLE(table1), GTK_WIDGET(phone_list_menu[i2-3]),
				      1, 2, i, i+1);
	 } else {
	    gtk_table_attach_defaults(GTK_TABLE(table1), GTK_WIDGET(label),
				      1, 2, i, i+1);
	 }
	 gtk_table_attach_defaults(GTK_TABLE(table1), GTK_WIDGET(address_text[i2]),
				   2, 3, i, i+1);
	 /*gtk_box_pack_start(GTK_BOX(vbox_temp1), hbox_temp, TRUE, TRUE, 0); */
      }
      if (i>8 && i<14) {
	 gtk_table_attach_defaults(GTK_TABLE(table2), GTK_WIDGET(label),
				   0, 1, i-9, i-8);
	 gtk_table_attach_defaults(GTK_TABLE(table2), GTK_WIDGET(address_text[i2]),
				   1, 2, i-9, i-8);
	 /*gtk_box_pack_start(GTK_BOX(vbox_temp2), hbox_temp, TRUE, TRUE, 0); */
      }
      if (i>13 && i<100) {
	 gtk_table_attach_defaults(GTK_TABLE(table3), GTK_WIDGET(label),
				   0, 1, i-14, i-13);
	 gtk_table_attach_defaults(GTK_TABLE(table3), GTK_WIDGET(address_text[i2]),
				   1, 2, i-14, i-13);
      }
   }
   /* Capture the TAB key to change focus with it */
   for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
      i1=order[i];
      i2=order[i+1];
      if (i2<NUM_ADDRESS_ENTRIES) {
	 gtk_signal_connect(GTK_OBJECT(address_text[i1]), "key_press_event",
			    GTK_SIGNAL_FUNC(cb_key_pressed), address_text[i2]);
      }
   }

   /* Put some radio buttons for selecting which number to display in view */
   group = NULL;
   for (i=0; i<NUM_PHONE_ENTRIES; i++) {
      radio_button[i] = gtk_radio_button_new(group);
      group = gtk_radio_button_group(GTK_RADIO_BUTTON(radio_button[i]));
      gtk_widget_set_usize(GTK_WIDGET(radio_button[i]), 5, 0);
      gtk_table_attach_defaults(GTK_TABLE(table1), GTK_WIDGET(radio_button[i]),
				0, 1, i+4, i+5);
   }

   /*The Quickview page */
   frame = gtk_frame_new(_("Quick View"));
   gtk_frame_set_label_align(GTK_FRAME(frame), 0.5, 0.0);
   gtk_box_pack_start(GTK_BOX(hbox_temp4), frame, TRUE, TRUE, 0);
   /*The text box on the right side */
   hbox_temp = gtk_hbox_new (FALSE, 0);
   gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
   gtk_container_add(GTK_CONTAINER(frame), hbox_temp);

   text = gtk_text_new(NULL, NULL);
   gtk_text_set_editable(GTK_TEXT(text), FALSE);
   gtk_text_set_word_wrap(GTK_TEXT(text), TRUE);
   vscrollbar = gtk_vscrollbar_new(GTK_TEXT(text)->vadj);
   gtk_box_pack_start(GTK_BOX(hbox_temp), text, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(hbox_temp), vscrollbar, FALSE, FALSE, 0);


   gtk_widget_show_all(vbox);
   gtk_widget_show_all(hbox);
   
   gtk_widget_hide(add_record_button);
   gtk_widget_hide(apply_record_button);

   get_pref(PREF_ADDRESS_NOTEBOOK_PAGE, &notebook_page, NULL);

   if ((notebook_page<4) && (notebook_page>-1)) {
      gtk_notebook_set_page(GTK_NOTEBOOK(notebook), notebook_page);
   }

   address_refresh();

   return 0;
}
