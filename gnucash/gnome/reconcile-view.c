/********************************************************************\
 * reconcile-view.c -- A view of accounts to be reconciled for      *
 *                     GnuCash.                                     *
 * Copyright (C) 1998,1999 Jeremy Collins                           *
 * Copyright (C) 1998-2000 Linas Vepstas                            *
 * Copyright (C) 2012 Robert Fewell                                 *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>

#include "gnc-date.h"
#include "qof.h"
#include "qofbook.h"
#include "Transaction.h"
#include "gnc-ui-util.h"
#include "gnc-prefs.h"
#include "reconcile-view.h"
#include "search-param.h"
#include "gnc-component-manager.h"

#define GNC_PREF_CHECK_CLEARED "check-cleared"

/* Signal codes */
enum
{
    TOGGLE_RECONCILED,
    LINE_SELECTED,
    DOUBLE_CLICK_SPLIT,
    LAST_SIGNAL
};


/** Static Globals ****************************************************/
static GNCQueryViewClass *parent_class = NULL;
static guint reconcile_view_signals[LAST_SIGNAL] = {0};

/** Static function declarations **************************************/
static void gnc_reconcile_view_init (GNCReconcileView *view);
static void gnc_reconcile_view_class_init (GNCReconcileViewClass *klass);
static void gnc_reconcile_view_finalize (GObject *object);
static gpointer gnc_reconcile_view_is_reconciled (gpointer item,
                                                  gpointer user_data);
static void gnc_reconcile_view_line_toggled (GNCQueryView *qview,
                                             gpointer item,
                                             gpointer user_data);
static void gnc_reconcile_view_double_click_entry (GNCQueryView *qview,
                                                   gpointer item,
                                                   gpointer user_data);
static void gnc_reconcile_view_row_selected (GNCQueryView *qview,
                                             gpointer item,
                                             gpointer user_data);
static gboolean gnc_reconcile_view_key_press_cb (GtkWidget *widget,
                                                 GdkEventKey *event,
                                                 gpointer user_data);
static gboolean gnc_reconcile_view_tooltip_cb (GNCQueryView *qview,
                                               gint x, gint y,
                                               gboolean keyboard_mode,
                                               GtkTooltip* tooltip,
                                               gpointer* user_data);

GType
gnc_reconcile_view_get_type (void)
{
    static GType gnc_reconcile_view_type = 0;

    if (gnc_reconcile_view_type == 0)
    {
        static const GTypeInfo gnc_reconcile_view_info =
        {
            sizeof (GNCReconcileViewClass),
            NULL,
            NULL,
            (GClassInitFunc) gnc_reconcile_view_class_init,
            NULL,
            NULL,
            sizeof (GNCReconcileView),
            0,
            (GInstanceInitFunc) gnc_reconcile_view_init
        };

        gnc_reconcile_view_type = g_type_register_static (GNC_TYPE_QUERY_VIEW,
                                                          "GncReconcileView",
                                                          &gnc_reconcile_view_info,
                                                          0);
    }
    return gnc_reconcile_view_type;
}


static gboolean
gnc_reconcile_view_tooltip_cb (GNCQueryView *qview, gint x, gint y,
                               gboolean keyboard_mode, GtkTooltip *tooltip,
                               gpointer *user_data)
{
    GtkTreeModel* model;
    GtkTreeIter iter;

    // If the Description is longer than can be display, show it in a tooltip
    if (gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW(qview), &x, &y,
                                           keyboard_mode, &model, NULL, &iter))
    {
        GtkTreeViewColumn *col;
        GList *cols;
        gint col_pos, col_width;
        gchar* desc_text = NULL;

        /* Are we in keyboard tooltip mode, displays tooltip below/above treeview CTRL+F1 */
        if (keyboard_mode == FALSE)
        {
            if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW(qview), x, y,
                                               NULL, &col, NULL, NULL) == FALSE)
                return FALSE;
        }
        else
            gtk_tree_view_get_cursor (GTK_TREE_VIEW(qview), NULL, &col);

        cols = gtk_tree_view_get_columns (GTK_TREE_VIEW(qview));
        col_width = gtk_tree_view_column_get_width (col);
        col_pos = g_list_index (cols, col);
        g_list_free (cols);

        /* If column is not description, do not show tooltip */
        if (col_pos != (REC_DESC - 1)) // allow for the pointer model column at 0
            return FALSE;

        gtk_tree_model_get (model, &iter, REC_DESC, &desc_text, -1);

        if (desc_text)
        {
            PangoLayout* layout;
            gint text_width;
            gint root_x, root_y;
            gint cur_x, cur_y;

            layout = gtk_widget_create_pango_layout (GTK_WIDGET(qview), desc_text);
            pango_layout_get_pixel_size (layout, &text_width, NULL);
            g_object_unref (layout);

            /* If text_width + 10 <= column_width, do not show tooltip */
            if ((text_width + 10) <= col_width)
            {
                g_free (desc_text);
                return FALSE;
            }

            if (keyboard_mode == FALSE)
            {
                GdkSeat *seat;
                GdkDevice *pointer;
                GtkWindow *tip_win = NULL;
                GdkWindow *parent_window;
                GList *win_list, *node;

                parent_window = gtk_widget_get_parent_window (GTK_WIDGET(qview));

                seat = gdk_display_get_default_seat (gdk_window_get_display (parent_window));
                pointer = gdk_seat_get_pointer (seat);

                gdk_window_get_device_position (parent_window, pointer, &cur_x, &cur_y, NULL);

                gdk_window_get_origin (parent_window, &root_x, &root_y);

                 /* Get a list of toplevel windows */
                win_list = gtk_window_list_toplevels ();

                /* Look for the gtk-tooltip window, we do this as gtk_widget_get_tooltip_window
                   does not seem to work for the default tooltip window, custom yes */
                for (node = win_list;  node != NULL;  node = node->next)
                {
                    if (g_strcmp0 (gtk_widget_get_name (node->data), "gtk-tooltip") == 0)
                        tip_win = node->data;
                }
                g_list_free (win_list);

                gtk_tooltip_set_text (tooltip, desc_text);

                if (GTK_IS_WINDOW(tip_win))
                {
                    GdkMonitor *mon;
                    GdkRectangle monitor;
                    GtkRequisition requisition;
                    gint x, y;

                    gtk_widget_get_preferred_size (GTK_WIDGET(tip_win), &requisition, NULL);

                    x = root_x + cur_x + 10;
                    y = root_y + cur_y + 10;

                    mon = gdk_display_get_monitor_at_point (gdk_window_get_display (parent_window), x, y);
                    gdk_monitor_get_geometry (mon, &monitor);

                    if (x + requisition.width > monitor.x + monitor.width)
                        x -= x - (monitor.x + monitor.width) + requisition.width;
                    else if (x < monitor.x)
                        x = monitor.x;

                    if (y + requisition.height > monitor.y + monitor.height)
                        y -= y - (monitor.y + monitor.height) + requisition.height;

                    gtk_window_move (tip_win, x, y);
                }
            }
            gtk_tooltip_set_text (tooltip, desc_text);
            g_free (desc_text);
            return TRUE;
        }
    }
    return FALSE;
}

gint
gnc_reconcile_view_get_column_width (GNCReconcileView *view, gint column)
{
    GNCQueryView      *qview = GNC_QUERY_VIEW(view);
    GtkTreeViewColumn *col;

    // allow for pointer model column at column 0
    col = gtk_tree_view_get_column (GTK_TREE_VIEW(qview), (column - 1));
    return  gtk_tree_view_column_get_width (col);
}

void
gnc_reconcile_view_add_padding (GNCReconcileView *view, gint column, gint xpadding)
{
    GNCQueryView      *qview = GNC_QUERY_VIEW(view);
    GtkTreeViewColumn *col;
    GList             *renderers;
    GtkCellRenderer   *cr0;
    gint xpad, ypad;

    // allow for pointer model column at column 0
    col = gtk_tree_view_get_column (GTK_TREE_VIEW(qview), (column - 1));
    renderers = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT(col));
    cr0 = g_list_nth_data (renderers, 0);
    g_list_free (renderers);

    gtk_cell_renderer_get_padding (cr0, &xpad, &ypad);
    gtk_cell_renderer_set_padding (cr0, xpadding, ypad);
}

/****************************************************************************\
 * gnc_reconcile_view_new                                                   *
 *   creates the account tree                                               *
 *                                                                          *
 * Args: account        - the account to use in filling up the splits.      *
 *       type           - the type of view, RECLIST_DEBIT or RECLIST_CREDIT *
 *       statement_date - date of statement                                 *
 * Returns: the account tree widget, or NULL if there was a problem.        *
\****************************************************************************/
static void
gnc_reconcile_view_construct (GNCReconcileView *view, Query *query)
{
    GNCQueryView      *qview = GNC_QUERY_VIEW(view);
    GtkTreeViewColumn *col;
    GtkTreeSelection  *selection;
    GList             *renderers;
    GtkCellRenderer   *cr0;
    gboolean           inv_sort = FALSE;

    if (view->view_type == RECLIST_CREDIT)
        inv_sort = TRUE;

    /* Construct the view */
    gnc_query_view_construct (qview, view->column_list, query);
    gnc_query_view_set_numerics (qview, TRUE, inv_sort);

    /* Set the description field to have spare space,
       REC_DESC -1 to allow for the pointer model column at 0 */
    col = gtk_tree_view_get_column (GTK_TREE_VIEW(qview), (REC_DESC - 1));
    gtk_tree_view_column_set_expand (col, TRUE);

    /* Get the renderer of the description column and set ellipsize value */
    renderers = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT(col));
    cr0 = g_list_nth_data (renderers, 0);
    g_list_free (renderers);
    g_object_set (cr0, "ellipsize", PANGO_ELLIPSIZE_END, NULL );

    gtk_widget_set_has_tooltip (GTK_WIDGET(qview), TRUE);

    /* Set the selection method */
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(qview));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

    /* Now set up the signals for the QueryView */
    g_signal_connect (G_OBJECT (qview), "column_toggled",
                      G_CALLBACK (gnc_reconcile_view_line_toggled), view);
    g_signal_connect (G_OBJECT(qview), "double_click_entry",
                      G_CALLBACK(gnc_reconcile_view_double_click_entry), view);
    g_signal_connect (G_OBJECT(qview), "row_selected",
                      G_CALLBACK(gnc_reconcile_view_row_selected), view);
    g_signal_connect (G_OBJECT(qview), "key_press_event",
                      G_CALLBACK(gnc_reconcile_view_key_press_cb), view);
    g_signal_connect (G_OBJECT(qview), "query-tooltip",
                      G_CALLBACK(gnc_reconcile_view_tooltip_cb), view);
}

static gint
sort_date_helper (time64 date_a, time64 date_b)
{
    gint ret = 0;

    if (date_a < date_b)
        ret = -1;
    else if (date_a > date_b)
        ret = 1;

    return ret;
}

static gint
sort_iter_compare_func (GtkTreeModel *model,
                        GtkTreeIter  *a,
                        GtkTreeIter  *b,
                        gpointer      user_data)
{
    gboolean rec_a, rec_b;
    Split   *split_a, *split_b;
    time64   date_a, date_b;
    gint     ret = 0;

    gtk_tree_model_get (model, a, REC_POINTER, &split_a, REC_RECN, &rec_a, -1);
    gtk_tree_model_get (model, b, REC_POINTER, &split_b, REC_RECN, &rec_b, -1);

    date_a = xaccTransGetDate (xaccSplitGetParent (split_a));
    date_b = xaccTransGetDate (xaccSplitGetParent (split_b));

    if (rec_a > rec_b)
        ret = -1;
    else if (rec_b > rec_a)
        ret = 1;
    else ret = sort_date_helper (date_a, date_b);

    return ret;
}

GtkWidget *
gnc_reconcile_view_new (Account *account, GNCReconcileViewType type,
                        time64 statement_date)
{
    GNCReconcileView  *view;
    GtkListStore      *liststore;
    GtkTreeSortable   *sortable;
    GtkTreeViewColumn *col;
    gboolean           include_children, auto_check;
    GList             *accounts = NULL;
    GList             *splits;
    Query             *query;
    QofNumericMatch    sign;

    g_return_val_if_fail (account, NULL);
    g_return_val_if_fail ((type == RECLIST_DEBIT) ||
                         (type == RECLIST_CREDIT), NULL);

    view = g_object_new (GNC_TYPE_RECONCILE_VIEW, NULL);

    /* Create the list store with 6 columns and add to treeview,
       column 0 will be a pointer to the entry */
    liststore = gtk_list_store_new (6, G_TYPE_POINTER, G_TYPE_STRING,
                                       G_TYPE_STRING, G_TYPE_STRING,
                                       G_TYPE_STRING,  G_TYPE_BOOLEAN);

    gtk_tree_view_set_model (GTK_TREE_VIEW(view), GTK_TREE_MODEL(liststore));
    g_object_unref (liststore);

    view->account = account;
    view->view_type = type;
    view->statement_date = statement_date;

    query = qof_query_create_for (GNC_ID_SPLIT);
    qof_query_set_book (query, gnc_get_current_book ());

    include_children = xaccAccountGetReconcileChildrenStatus (account);
    if (include_children)
        accounts = gnc_account_get_descendants (account);

    /* match the account */
    accounts = g_list_prepend (accounts, account);

    xaccQueryAddAccountMatch (query, accounts, QOF_GUID_MATCH_ANY, QOF_QUERY_AND);

    g_list_free (accounts);

    sign = (type == RECLIST_CREDIT) ? QOF_NUMERIC_MATCH_CREDIT :
                                      QOF_NUMERIC_MATCH_DEBIT;

    xaccQueryAddNumericMatch (query, gnc_numeric_zero (), sign, QOF_COMPARE_GTE,
                              QOF_QUERY_AND, SPLIT_AMOUNT, NULL);

    /* limit the matches only to Cleared and Non-reconciled splits */
    xaccQueryAddClearedMatch (query, CLEARED_NO | CLEARED_CLEARED, QOF_QUERY_AND);

    /* Initialize the QueryList */
    gnc_reconcile_view_construct (view, query);

    /* find the list of splits to auto-reconcile */
    auto_check = gnc_prefs_get_bool (GNC_PREFS_GROUP_RECONCILE, GNC_PREF_CHECK_CLEARED);

    if (auto_check)
    {
        time64 statement_date_day_end = gnc_time64_get_day_end (statement_date);
        for (splits = qof_query_run (query); splits; splits = splits->next)
        {
            Split *split = splits->data;
            char recn = xaccSplitGetReconcile (split);
            time64 trans_date = xaccTransGetDate (xaccSplitGetParent (split));

            /* Just an extra verification that our query is correct ;) */
            g_assert (recn == NREC || recn == CREC);

            if (recn == CREC && gnc_difftime (trans_date, statement_date_day_end) <= 0)
                g_hash_table_insert (view->reconciled, split, split);
        }
    }

    /* set up a separate sort function for the recn column as it is
     * derived from a search function */
    sortable = GTK_TREE_SORTABLE(gtk_tree_view_get_model (GTK_TREE_VIEW(view)));
    col = gtk_tree_view_get_column (GTK_TREE_VIEW(view), REC_RECN -1);
    gtk_tree_sortable_set_sort_func (sortable, REC_RECN, sort_iter_compare_func,
                                     GINT_TO_POINTER (REC_RECN), NULL);

    /* Free the query -- we don't need it anymore */
    qof_query_destroy (query);

    return GTK_WIDGET(view);
}

static void
gnc_reconcile_view_init (GNCReconcileView *view)
{
    GNCSearchParamSimple *param;
    GList                *columns = NULL;
    gboolean num_action = qof_book_use_split_action_for_num_field (gnc_get_current_book());

    view->reconciled = g_hash_table_new (NULL, NULL);
    view->account = NULL;
    view->sibling = NULL;

    param = gnc_search_param_simple_new ();
    gnc_search_param_set_param_fcn (param, QOF_TYPE_BOOLEAN,
                                    gnc_reconcile_view_is_reconciled, view);
    gnc_search_param_set_title ((GNCSearchParam *) param, C_("Column header for 'Reconciled'", "R"));
    gnc_search_param_set_justify ((GNCSearchParam *) param, GTK_JUSTIFY_CENTER);
    gnc_search_param_set_passive ((GNCSearchParam *) param, FALSE);
    gnc_search_param_set_non_resizeable ((GNCSearchParam *) param, TRUE);
    columns = g_list_prepend (columns, param);

    columns = gnc_search_param_prepend_with_justify (columns, _("Amount"),
                                                     GTK_JUSTIFY_RIGHT,
                                                     NULL, GNC_ID_SPLIT,
                                                     SPLIT_AMOUNT, NULL);
    columns = gnc_search_param_prepend (columns, _("Description"), NULL,
                                        GNC_ID_SPLIT, SPLIT_TRANS,
                                        TRANS_DESCRIPTION, NULL);
    columns = num_action ?
              gnc_search_param_prepend_with_justify (columns, _("Num"),
                                                     GTK_JUSTIFY_CENTER,
                                                     NULL, GNC_ID_SPLIT,
                                                     SPLIT_ACTION, NULL) :
              gnc_search_param_prepend_with_justify (columns, _("Num"),
                                                     GTK_JUSTIFY_CENTER,
                                                     NULL, GNC_ID_SPLIT,
                                                     SPLIT_TRANS, TRANS_NUM, NULL);
    columns = gnc_search_param_prepend (columns, _("Date"),
                                        NULL, GNC_ID_SPLIT,
                                        SPLIT_TRANS,
                                        TRANS_DATE_POSTED, NULL);

    view->column_list = columns;
}

static void
gnc_reconcile_view_class_init (GNCReconcileViewClass *klass)
{
    GObjectClass    *object_class;

    object_class =  G_OBJECT_CLASS(klass);

    parent_class = g_type_class_peek_parent (klass);

    reconcile_view_signals[TOGGLE_RECONCILED] =
        g_signal_new ("toggle_reconciled",
                      G_OBJECT_CLASS_TYPE(object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET(GNCReconcileViewClass,
                                      toggle_reconciled),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1,
                      G_TYPE_POINTER);

    reconcile_view_signals[LINE_SELECTED] =
        g_signal_new ("line_selected",
                      G_OBJECT_CLASS_TYPE(object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET(GNCReconcileViewClass,
                                      line_selected),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1,
                      G_TYPE_POINTER);

    reconcile_view_signals[DOUBLE_CLICK_SPLIT] =
        g_signal_new ("double_click_split",
                      G_OBJECT_CLASS_TYPE(object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET(GNCReconcileViewClass,
                                      double_click_split),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1,
                      G_TYPE_POINTER);

    object_class->finalize = gnc_reconcile_view_finalize;

    klass->toggle_reconciled = NULL;
    klass->line_selected = NULL;
    klass->double_click_split = NULL;
}

static void
gnc_reconcile_view_toggle_split (GNCReconcileView *view, Split *split)
{
    Split *current;

    g_return_if_fail (GNC_IS_RECONCILE_VIEW(view));
    g_return_if_fail (view->reconciled != NULL);

    current = g_hash_table_lookup (view->reconciled, split);

    if (current == NULL)
        g_hash_table_insert (view->reconciled, split, split);
    else
        g_hash_table_remove (view->reconciled, split);
}

static void
gnc_reconcile_view_toggle (GNCReconcileView *view, Split *split)
{
    g_return_if_fail (GNC_IS_RECONCILE_VIEW(view));
    g_return_if_fail (view->reconciled != NULL);

    gnc_reconcile_view_toggle_split (view, split);

    g_signal_emit (G_OBJECT(view),
                   reconcile_view_signals[TOGGLE_RECONCILED], 0, split);
}

static gboolean
follow_select_tree_path (GNCReconcileView *view)
{
    if (view->rowref)
    {
        GtkTreePath      *tree_path = gtk_tree_row_reference_get_path (view->rowref);
        GNCQueryView      qview = view->qview;
        GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(&qview));

        gtk_tree_selection_unselect_all (selection);
        gtk_tree_selection_select_path (selection, tree_path);

        gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW(&qview),
                                      tree_path, NULL, FALSE, 0.0, 0.0);

        gtk_tree_path_free (tree_path);
        gtk_tree_row_reference_free (view->rowref);
        view->rowref = NULL;
    }
    return FALSE;
}

static void
gnc_reconcile_view_line_toggled (GNCQueryView *qview,
                                 gpointer item,
                                 gpointer user_data)
{
    GNCReconcileView *view;
    GtkTreeModel     *model;
    GtkTreeIter       iter;
    gpointer          entry;
    GtkTreePath      *tree_path;

    g_return_if_fail (user_data);
    g_return_if_fail (GNC_IS_QUERY_VIEW(qview));

    view = user_data;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW(qview));
    gtk_tree_model_iter_nth_child (model, &iter, NULL, qview->toggled_row);

    tree_path = gtk_tree_model_get_path (model, &iter);
    view->rowref = gtk_tree_row_reference_new (model, tree_path);
    gtk_tree_path_free (tree_path);

    gtk_list_store_set (GTK_LIST_STORE(model), &iter, qview->toggled_column,
                                                      GPOINTER_TO_INT(item), -1);

    tree_path = gtk_tree_row_reference_get_path (view->rowref);

    if (gtk_tree_model_get_iter (model, &iter, tree_path))
    {
        gtk_tree_model_get (model, &iter, REC_POINTER, &entry, -1);
        gnc_reconcile_view_toggle (view, entry);
    }

    // See if sorting on rec column, -1 to allow for the model pointer column at 0
    if (qview->sort_column == REC_RECN - 1)
        g_idle_add ((GSourceFunc)follow_select_tree_path, view);
    else
    {
        gtk_tree_row_reference_free (view->rowref);
        view->rowref = NULL;
    }

    gtk_tree_path_free (tree_path);
}

static void
gnc_reconcile_view_double_click_entry (GNCQueryView *qview,
                                       gpointer item,
                                       gpointer user_data)
{
    GNCReconcileView *view;

    /* item is the entry */
    g_return_if_fail (user_data);
    g_return_if_fail (GNC_IS_QUERY_VIEW(qview));

    view = user_data;

    g_signal_emit(G_OBJECT(view),
                  reconcile_view_signals[DOUBLE_CLICK_SPLIT], 0, item);
}

static void
gnc_reconcile_view_row_selected (GNCQueryView *qview,
                                 gpointer item,
                                 gpointer user_data)
{
    GNCReconcileView *view;

    /* item is the number of selected entries */
    g_return_if_fail (user_data);
    g_return_if_fail (GNC_IS_QUERY_VIEW(qview));

    view = user_data;

    g_signal_emit (G_OBJECT(view),
                   reconcile_view_signals[LINE_SELECTED], 0, item);
}

void
gnc_reconcile_view_set_list (GNCReconcileView  *view, gboolean reconcile)
{
    GNCQueryView      *qview = GNC_QUERY_VIEW(view);
    GtkTreeSelection  *selection;
    GtkTreeModel      *model;
    gpointer           entry;
    gboolean           toggled;
    GList             *node;
    GList             *list_of_rows;
    GList             *rr_list = NULL;
    GtkTreePath       *last_tree_path = NULL;

    model =  gtk_tree_view_get_model (GTK_TREE_VIEW(qview));
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(qview));
    list_of_rows = gtk_tree_selection_get_selected_rows (selection, &model);

    /* First create a list of Row references */
    for (node = list_of_rows; node; node = node->next)
    {
        GtkTreeRowReference *rowref = gtk_tree_row_reference_new (model, node->data);
        rr_list = g_list_append (rr_list, rowref);
        gtk_tree_path_free (node->data);
    }

    for (node = rr_list; node; node = node->next)
    {
        GtkTreeIter          iter;
        GtkTreePath         *path;
        GtkTreeRowReference *rowref = node->data;

        path = gtk_tree_row_reference_get_path (rowref);

        if (gtk_tree_model_get_iter (model, &iter, path))
        {
            /* now iter is a valid row iterator */
            gtk_tree_model_get (model, &iter, REC_POINTER, &entry,
                                              REC_RECN, &toggled, -1);

            gtk_list_store_set (GTK_LIST_STORE(model), &iter, REC_RECN, reconcile, -1);

            if (last_tree_path)
                gtk_tree_path_free (last_tree_path);
            last_tree_path = gtk_tree_row_reference_get_path (rowref);

            if (reconcile != toggled)
                gnc_reconcile_view_toggle (view, entry);
        }
        gtk_tree_path_free (path);
    }

    if (last_tree_path)
    {
        // See if sorting on rec column, -1 to allow for the model pointer column at 0
        if (qview->sort_column == REC_RECN -1)
        {
            gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW(qview),
                                          last_tree_path, NULL, FALSE, 0.0, 0.0);
        }
        gtk_tree_path_free (last_tree_path);
        last_tree_path = NULL;
    }
    g_list_foreach (rr_list, (GFunc) gtk_tree_row_reference_free, NULL);
    g_list_free (rr_list);

    // Out of site toggles on selected rows may not appear correctly drawn so
    // queue a draw for the treeview widget
    gtk_widget_queue_draw (GTK_WIDGET(qview));
    g_list_free (list_of_rows);
}

gint
gnc_reconcile_view_num_selected (GNCReconcileView  *view )
{
    GNCQueryView      *qview = GNC_QUERY_VIEW(view);
    GtkTreeSelection  *selection;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(qview));
    return gtk_tree_selection_count_selected_rows (selection);
}

static gboolean
gnc_reconcile_view_set_toggle (GNCReconcileView  *view)
{
    GNCQueryView      *qview = GNC_QUERY_VIEW(view);
    GtkTreeSelection  *selection;
    GtkTreeModel      *model;
    gboolean           toggled;
    GList             *node;
    GList             *list_of_rows;
    gint               num_toggled = 0;
    gint               num_selected = 0;

    model =  gtk_tree_view_get_model (GTK_TREE_VIEW(qview));
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(qview));
    list_of_rows = gtk_tree_selection_get_selected_rows (selection, &model);
    num_selected = gtk_tree_selection_count_selected_rows (selection);

    /* We get a list of TreePaths */
    for (node = list_of_rows; node; node = node->next)
    {
        GtkTreeIter iter;
        toggled = FALSE;
        if (gtk_tree_model_get_iter (model, &iter, node->data))
        {
            /* now iter is a valid row iterator */
            gtk_tree_model_get (model, &iter, REC_RECN, &toggled, -1);

            if (toggled)
                num_toggled++;
        }
        gtk_tree_path_free (node->data);
    }
    g_list_free (list_of_rows);

    if (num_toggled == num_selected)
        return FALSE;
    else
        return TRUE;
}

static gboolean
gnc_reconcile_view_key_press_cb (GtkWidget *widget, GdkEventKey *event,
                                 gpointer user_data)
{
    GNCReconcileView  *view = GNC_RECONCILE_VIEW(user_data);
    gboolean           toggle;

    switch (event->keyval)
    {
    case GDK_KEY_space:
        g_signal_stop_emission_by_name (widget, "key_press_event");

        toggle = gnc_reconcile_view_set_toggle (view);
        gnc_reconcile_view_set_list (view, toggle);
        return TRUE;
        break;

    default:
        return FALSE;
    }
}

static void
gnc_reconcile_view_finalize (GObject *object)
{
    GNCReconcileView *view = GNC_RECONCILE_VIEW(object);

    g_list_free (view->column_list);
    if (view->reconciled != NULL)
    {
        g_hash_table_destroy (view->reconciled);
        view->reconciled = NULL;
    }
    G_OBJECT_CLASS(parent_class)->finalize (object);
}

gint
gnc_reconcile_view_get_num_splits (GNCReconcileView *view)
{
    g_return_val_if_fail (view != NULL, 0);
    g_return_val_if_fail (GNC_IS_RECONCILE_VIEW(view), 0);

    return gnc_query_view_get_num_entries (GNC_QUERY_VIEW(view));
}

Split *
gnc_reconcile_view_get_current_split (GNCReconcileView *view)
{
    g_return_val_if_fail (view != NULL, NULL);
    g_return_val_if_fail (GNC_IS_RECONCILE_VIEW(view), NULL);

    return gnc_query_view_get_selected_entry (GNC_QUERY_VIEW(view));
}

/********************************************************************\
 * gnc_reconcile_view_is_reconciled                                 *
 *   Is the item a reconciled split?                                *
 *                                                                  *
 * Args: item      - the split to be checked                        *
 *       user_data - a pointer to the GNCReconcileView              *
 * Returns: whether the split is to be reconciled.                  *
\********************************************************************/
static gpointer
gnc_reconcile_view_is_reconciled (gpointer item, gpointer user_data)
{
    GNCReconcileView *view = user_data;
    Split            *current;

    g_return_val_if_fail (item, NULL);
    g_return_val_if_fail (view, NULL);
    g_return_val_if_fail (GNC_IS_RECONCILE_VIEW(view), NULL);

    if (!view->reconciled)
        return NULL;

    current = g_hash_table_lookup (view->reconciled, item);
    return GINT_TO_POINTER(current != NULL);
}

/********************************************************************\
 * gnc_reconcile_view_refresh                                       *
 *   refreshes the view                                             *
 *                                                                  *
 * Args: view - view to refresh                                     *
 * Returns: nothing                                                 *
\********************************************************************/
static gboolean
grv_refresh_helper (gpointer key, gpointer value, gpointer user_data)
{
    GNCQueryView     *qview = user_data;

    return !gnc_query_view_item_in_view (qview, key);
}

void
gnc_reconcile_view_refresh (GNCReconcileView *view)
{
    GNCQueryView      *qview;

    g_return_if_fail (view != NULL);
    g_return_if_fail (GNC_IS_RECONCILE_VIEW(view));

    qview = GNC_QUERY_VIEW(view);
    gnc_query_view_refresh (qview);

    /* Ensure last selected split, if any, can be seen */
    gnc_query_force_scroll_to_selection (qview);

    /* Now verify that everything in the reconcile hash is still in qview */
    if (view->reconciled)
        g_hash_table_foreach_remove (view->reconciled, grv_refresh_helper, qview);
}

/********************************************************************\
 * gnc_reconcile_view_reconciled_balance                            *
 *   returns the reconciled balance of the view                     *
 *                                                                  *
 * Args: view - view to get reconciled balance of                   *
 * Returns: reconciled balance (gnc_numeric)                        *
\********************************************************************/
static void
grv_balance_hash_helper (gpointer key, gpointer value, gpointer user_data)
{
    Split       *split = key;
    gnc_numeric *total = user_data;

    *total = gnc_numeric_add_fixed (*total, xaccSplitGetAmount (split));
}

gnc_numeric
gnc_reconcile_view_reconciled_balance (GNCReconcileView *view)
{
    gnc_numeric total = gnc_numeric_zero ();

    g_return_val_if_fail (view != NULL, total);
    g_return_val_if_fail (GNC_IS_RECONCILE_VIEW(view), total);

    if (view->reconciled == NULL)
        return total;

    g_hash_table_foreach (view->reconciled, grv_balance_hash_helper, &total);

    return gnc_numeric_abs (total);
}

/********************************************************************\
 * gnc_reconcile_view_commit                                        *
 *   Commit the reconcile information in the view. Only change the  *
 *   state of those items marked as reconciled.  All others should  *
 *   retain their previous state (none, cleared, voided, etc.).     *
 *                                                                  *
 * Args: view - view to commit                                      *
 *       date - date to set as the reconcile date                   *
 * Returns: nothing                                                 *
\********************************************************************/
static void
grv_commit_hash_helper (gpointer key, gpointer value, gpointer user_data)
{
    Split  *split = key;
    time64 *date = user_data;

    xaccSplitSetReconcile (split, YREC);
    xaccSplitSetDateReconciledSecs (split, *date);
}

void
gnc_reconcile_view_commit (GNCReconcileView *view, time64 date)
{
    g_return_if_fail (view != NULL);
    g_return_if_fail (GNC_IS_RECONCILE_VIEW(view));

    if (view->reconciled == NULL)
        return;

    gnc_suspend_gui_refresh ();
    g_hash_table_foreach (view->reconciled, grv_commit_hash_helper, &date);
    gnc_resume_gui_refresh ();
}

/********************************************************************\
 * gnc_reconcile_view_postpone                                      *
 *   postpone the reconcile information in the view by setting      *
 *   reconciled splits to cleared status                            *
 *                                                                  *
 * Args: view - view to commit                                      *
 * Returns: nothing                                                 *
\********************************************************************/
void
gnc_reconcile_view_postpone (GNCReconcileView *view)
{
    GtkTreeModel *model;
    GtkTreeIter   iter;
    int           num_splits;
    int           i;
    gpointer      entry = NULL;

    g_return_if_fail (view != NULL);
    g_return_if_fail (GNC_IS_RECONCILE_VIEW(view));

    if (view->reconciled == NULL)
        return;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW(GNC_QUERY_VIEW(view)));
    gtk_tree_model_get_iter_first (model, &iter);

    num_splits = gnc_query_view_get_num_entries (GNC_QUERY_VIEW(view));

    gnc_suspend_gui_refresh ();
    for (i = 0; i < num_splits; i++)
    {
        char recn;

        gtk_tree_model_get (model, &iter, REC_POINTER, &entry, -1);

        // Don't change splits past reconciliation date that haven't been
        // set to be reconciled
        if (gnc_difftime (view->statement_date,
              xaccTransGetDate (xaccSplitGetParent (entry))) >= 0 ||
                g_hash_table_lookup (view->reconciled, entry))
        {
            recn = g_hash_table_lookup (view->reconciled, entry) ? CREC : NREC;
            xaccSplitSetReconcile (entry, recn);
        }
        gtk_tree_model_iter_next (model, &iter);
    }
    gnc_resume_gui_refresh ();
}

/********************************************************************\
 * gnc_reconcile_view_unselect_all                                  *
 *   unselect all splits in the view                                *
 *                                                                  *
 * Args: view - view to unselect all                                *
 * Returns: nothing                                                 *
\********************************************************************/
void
gnc_reconcile_view_unselect_all (GNCReconcileView *view)
{
    g_return_if_fail (view != NULL);
    g_return_if_fail (GNC_IS_RECONCILE_VIEW(view));

    gnc_query_view_unselect_all (GNC_QUERY_VIEW(view));
}

/********************************************************************\
 * gnc_reconcile_view_changed                                       *
 *   returns true if any splits have been reconciled                *
 *                                                                  *
 * Args: view - view to get changed status for                      *
 * Returns: true if any reconciled splits                           *
\********************************************************************/
gboolean
gnc_reconcile_view_changed (GNCReconcileView *view)
{
    g_return_val_if_fail (view != NULL, FALSE);
    g_return_val_if_fail (GNC_IS_RECONCILE_VIEW(view), FALSE);

    return g_hash_table_size (view->reconciled) != 0;
}
