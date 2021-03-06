/*
 *  Copyright (C) 2013 Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef RB_LIST_MODEL_H
#define RB_LIST_MODEL_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_LIST_MODEL         (rb_list_model_get_type ())
#define RB_LIST_MODEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIST_MODEL, RBListModel))
#define RB_LIST_MODEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIST_MODEL, RBListModelClass))
#define RB_IS_LIST_MODEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIST_MODEL))
#define RB_IS_LIST_MODEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIST_MODEL))
#define RB_LIST_MODEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIST_MODEL, RBListModelClass))

typedef struct _RBListModel RBListModel;
typedef struct _RBListModelClass RBListModelClass;
typedef struct _RBListModelPrivate RBListModelPrivate;

GType		rb_list_model_get_type		(void);

RBListModel *	rb_list_model_new		(GType item_type, GDestroyNotify destroy_item);

GType		rb_list_model_get_item_type	(RBListModel *model);
int		rb_list_model_n_items		(RBListModel *model);
gpointer	rb_list_model_get		(RBListModel *model, int index);
int		rb_list_model_find		(RBListModel *model, gpointer item);

void		rb_list_model_insert 		(RBListModel *model, int index, gpointer item);
void		rb_list_model_append		(RBListModel *model, gpointer item);
void		rb_list_model_prepend		(RBListModel *model, gpointer item);

void		rb_list_model_remove 		(RBListModel *model, int index);
void		rb_list_model_remove_item 	(RBListModel *model, gpointer item);

G_END_DECLS

#endif /* RB_LIST_MODEL_H */
