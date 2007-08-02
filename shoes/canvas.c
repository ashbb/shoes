//
// shoes/canvas.c
// Ruby methods for all the drawing ops.
//
#include "shoes/internal.h"
#include "shoes/canvas.h"
#include "shoes/app.h"
#include "shoes/ruby.h"

#define SETUP() \
  shoes_canvas *canvas; \
  cairo_t *cr; \
  Data_Get_Struct(self, shoes_canvas, canvas); \
  cr = canvas->cr

#ifdef SHOES_GTK
static void
shoes_canvas_gtk_paint (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{ 
  GtkRequisition req;
  VALUE c = (VALUE)data;
  shoes_canvas *canvas;
  Data_Get_Struct(c, shoes_canvas, canvas);
  canvas->slot.expose = event;
  // TODO: Move to resize handler
  // gtk_window_get_size(GTK_WINDOW(app->kit.window), &app->width, &app->height);
  shoes_canvas_paint(c);
}

static gboolean
shoes_canvas_gtk_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{ 
  GdkModifierType state;
  VALUE c = (VALUE)data;
  if (!event->is_hint)
  {
    state = (GdkModifierType)event->state;
    shoes_canvas_send_motion(c, (int)event->x, (int)event->y);
  }
  return TRUE;
}

static gboolean
shoes_canvas_gtk_button(GtkWidget *widget, GdkEventButton *event, gpointer data)
{ 
  VALUE c = (VALUE)data;
  if (event->type == GDK_BUTTON_PRESS)
  {
    shoes_canvas_send_click(c, event->button, event->x, event->y);
  }
  else if (event->type == GDK_BUTTON_RELEASE)
  {
    shoes_canvas_send_release(c, event->button, event->x, event->y);
  }
  return TRUE;
}
#endif

void
shoes_slot_init(VALUE c, APPSLOT *parent, int width, int height)
{
  shoes_canvas *canvas;
  APPSLOT *slot;
  Data_Get_Struct(c, shoes_canvas, canvas);
  slot = &canvas->slot;

#ifdef SHOES_GTK
  slot->box = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(slot->box), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(slot->box, width, height);
  slot->canvas = gtk_layout_new(NULL, NULL);
  gtk_widget_set_events(slot->canvas,
    gtk_widget_get_events(slot->canvas) | GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK);
  g_signal_connect(G_OBJECT(slot->canvas), "expose-event",
                   G_CALLBACK(shoes_canvas_gtk_paint), (gpointer)c);
  g_signal_connect(G_OBJECT(slot->canvas), "motion-notify-event",
                   G_CALLBACK(shoes_canvas_gtk_motion), (gpointer)c);
  g_signal_connect(G_OBJECT(slot->canvas), "button-press-event",
                   G_CALLBACK(shoes_canvas_gtk_button), (gpointer)c);
  g_signal_connect(G_OBJECT(slot->canvas), "button-release-event",
                   G_CALLBACK(shoes_canvas_gtk_button), (gpointer)c);
  gtk_container_add(GTK_CONTAINER(parent->canvas), slot->box);
  gtk_container_add(GTK_CONTAINER(slot->box), slot->canvas);
  GTK_LAYOUT(slot->canvas)->hadjustment->step_increment = 5;
  GTK_LAYOUT(slot->canvas)->vadjustment->step_increment = 5;
  slot->expose = NULL;
#endif

#ifdef SHOES_QUARTZ
  slot->controls = parent->controls;
  shoes_slot_quartz_create(c, parent, width, height);
#endif

#ifdef SHOES_WIN32
  slot->controls = parent->controls;
  slot->window = parent->window;
  slot->dc = parent->dc;
#endif

  INFO("shoes_slot_init(DONE)\n", 0);
}

cairo_t *
shoes_cairo_create(APPSLOT *slot, int width, int height, int border)
{
  cairo_t *cr;
#ifdef SHOES_GTK
  cr = gdk_cairo_create(GTK_LAYOUT(slot->canvas)->bin_window);
#endif
#ifdef SHOES_WIN32
  cr = cairo_create(slot->surface);
#endif
#ifdef SHOES_QUARTZ
  cr = cairo_create(slot->surface);
#endif
  cairo_save(cr);
  cairo_set_source_rgb(cr,1,1,1);
  cairo_set_line_width(cr,1.0);
  cairo_rectangle(cr,0,0,4000,4000);
  cairo_fill(cr);
  return cr;
}

void
shoes_canvas_paint(VALUE self)
{
  shoes_code code = SHOES_OK;

  if (self == Qnil)
    return;

  SETUP();

#ifdef SHOES_QUARTZ
  canvas->slot.surface = cairo_quartz_surface_create_for_cg_context(canvas->slot.context, canvas->width, canvas->height);
#endif

#ifdef SHOES_WIN32
  PAINTSTRUCT paint_struct;
  int width, height;
  HBITMAP bitmap;
  width = canvas->width; height = canvas->height;
  HDC hdc = BeginPaint(canvas->slot.window, &paint_struct);
  canvas->slot.dc = CreateCompatibleDC(hdc);
  bitmap = CreateCompatibleBitmap(hdc, width, height);
  SelectObject(canvas->slot.dc, bitmap);
  canvas->slot.surface = cairo_win32_surface_create(canvas->slot.dc);
#endif

  INFO("shoes_cairo_create: (%d, %d)\n", canvas->width, canvas->height);
  canvas->cr = cr = shoes_cairo_create(&canvas->slot, canvas->width, canvas->height, 0);
  shoes_canvas_draw(self, self, Qnil);

  cairo_restore(cr);

  if (cairo_status(cr)) {
    QUIT("Cairo is unhappy: %s\n", cairo_status_to_string (cairo_status (cr)));
  }

  cairo_destroy(cr);
  canvas->cr = NULL;

#ifdef SHOES_QUARTZ
  cairo_surface_destroy(canvas->slot.surface);
#endif

#ifdef SHOES_WIN32
  BitBlt(hdc, 0, 0, width, height, canvas->slot.dc, 0, 0, SRCCOPY);
  cairo_surface_destroy(canvas->slot.surface);
  EndPaint(canvas->slot.window, &paint_struct);
  DeleteObject(canvas->slot.dc);
#endif

quit:
  return;
}

void
shoes_canvas_shape_do(shoes_canvas *canvas, double x, double y)
{
  cairo_save(canvas->cr);
  if (canvas->mode == s_center)
  {
    cairo_translate(canvas->cr, x, y);
    cairo_transform(canvas->cr, canvas->tf);
  }
  else
  {
    cairo_transform(canvas->cr, canvas->tf);
    cairo_translate(canvas->cr, x, y);
  }
}

static VALUE
shoes_canvas_shape_end(VALUE self)
{
  VALUE shape;
  shoes_canvas *canvas;
  Data_Get_Struct(self, shoes_canvas, canvas);
  cairo_restore(canvas->cr);
  shape = shoes_path_new(cairo_copy_path(canvas->cr), self);
  rb_ary_push(canvas->contents, shape);
  return shape;
}

static void
shoes_canvas_mark(shoes_canvas *canvas)
{
  rb_gc_mark_maybe(canvas->contents);
  rb_gc_mark_maybe(canvas->attr);
  rb_gc_mark_maybe(canvas->parent);
}

static void
shoes_canvas_free(shoes_canvas *canvas)
{
  free(canvas->gr);
  free(canvas);
}

VALUE
shoes_canvas_alloc(VALUE klass)
{
  shoes_canvas *canvas = SHOE_ALLOC(shoes_canvas);
  SHOE_MEMZERO(canvas, shoes_canvas, 1);
  canvas->width = 0;
  canvas->height = 0;
  canvas->grl = 1;
  canvas->grt = 8;
  canvas->gr = SHOE_ALLOC_N(cairo_matrix_t, canvas->grt);
  cairo_matrix_init_identity(canvas->gr);
  VALUE rb_canvas = Data_Wrap_Struct(klass, shoes_canvas_mark, shoes_canvas_free, canvas);
  return rb_canvas;
}

void
shoes_canvas_clear(VALUE self)
{
  shoes_canvas *canvas;
  Data_Get_Struct(self, shoes_canvas, canvas);
  if (canvas->cr != NULL)
    cairo_destroy(canvas->cr);
  canvas->cr = cairo_create(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1));;
  canvas->fg.r = 0.;
  canvas->fg.g = 0.;
  canvas->fg.b = 0.;
  canvas->fg.a = 1.;
  canvas->fg.on = TRUE;
  canvas->bg.r = 0.;
  canvas->bg.g = 0.;
  canvas->bg.b = 0.;
  canvas->bg.a = 1.;
  canvas->bg.on = TRUE;
  canvas->mode = s_center;
  canvas->parent = Qnil;
  canvas->attr = Qnil;
  canvas->grl = 1;
  cairo_matrix_init_identity(canvas->gr);
  canvas->tf = canvas->gr;
  canvas->contents = rb_ary_new();
  canvas->x = 0.0;
  canvas->y = 0.0;
  canvas->cx = 0.0;
  canvas->cy = 0.0;
  canvas->endy = 0.0;
  canvas->endx = 0.0;
  canvas->fully = 0.0;
  canvas->click = Qnil;
  canvas->release = Qnil;
  canvas->motion = Qnil;
  canvas->release = Qnil;
  canvas->keypress = Qnil;
#ifdef SHOES_GTK
  canvas->layout = NULL;
#endif
}

shoes_canvas *
shoes_canvas_init(VALUE self, APPSLOT slot, VALUE attr, int width, int height)
{
  shoes_canvas *canvas;
  Data_Get_Struct(self, shoes_canvas, canvas);
  // canvas->slot = slot;
  canvas->attr = attr;
  canvas->width = width;
  canvas->height = height;
  return canvas;
}

VALUE
shoes_canvas_nostroke(VALUE self)
{
  SETUP();
  canvas->fg.on = FALSE;
  return self;
}

VALUE
shoes_canvas_stroke(int argc, VALUE *argv, VALUE self)
{
  VALUE _r, _g, _b, _a;
  SETUP();

  rb_scan_args(argc, argv, "31", &_r, &_g, &_b, &_a);
  canvas->fg.on = TRUE;
  canvas->fg.r = NUM2DBL(_r);
  canvas->fg.g = NUM2DBL(_g);
  canvas->fg.b = NUM2DBL(_b);
  canvas->fg.a = 1.0;
  if (!NIL_P(_a)) canvas->fg.a = NUM2DBL(_a);

  return self;
}

VALUE
shoes_canvas_strokewidth(VALUE self, VALUE _w)
{
  double w = NUM2DBL(_w);
  SETUP();
  canvas->sw = w;
  return self;
}

VALUE
shoes_canvas_nofill(VALUE self)
{
  SETUP();
  canvas->bg.on = FALSE;
  return self;
}

VALUE
shoes_canvas_fill(int argc, VALUE *argv, VALUE self)
{
  VALUE _r, _g, _b, _a;
  SETUP();

  rb_scan_args(argc, argv, "31", &_r, &_g, &_b, &_a);
  canvas->bg.on = TRUE;
  canvas->bg.r = NUM2DBL(_r);
  canvas->bg.g = NUM2DBL(_g);
  canvas->bg.b = NUM2DBL(_b);
  canvas->bg.a = 1.0;
  if (!NIL_P(_a)) canvas->bg.a = NUM2DBL(_a);

  return self;
}

VALUE
shoes_canvas_rect(int argc, VALUE *argv, VALUE self)
{
  VALUE _x, _y, _w, _h, _r;
  double x, y, w, h, r, rc;
  double bezier = 0.55228475;
  SETUP();

  rb_scan_args(argc, argv, "41", &_x, &_y, &_w, &_h, &_r);
  x = NUM2DBL(_x);
  y = NUM2DBL(_y);
  w = NUM2DBL(_w);
  h = NUM2DBL(_h);
  r = 0.0;
  if (!NIL_P(_r)) r = NUM2DBL(_r);

  rc = bezier * r;
  shoes_canvas_shape_do(canvas, x + (w / 2.), y + (h / 2.));
  x = -w / 2.;
  y = -h / 2.;
  cairo_new_path(cr);
  cairo_move_to(cr, x + r, y);
  cairo_rel_line_to(cr, w - 2 * r, 0.0);
  cairo_rel_curve_to(cr, rc, 0.0, r, rc, r, r);
  cairo_rel_line_to(cr, 0, h - 2 * r);
  cairo_rel_curve_to(cr, 0.0, rc, rc - r, r, -r, r);
  cairo_rel_line_to(cr, -w + 2 * r, 0);
  cairo_rel_curve_to(cr, -rc, 0, -r, -rc, -r, -r);
  cairo_rel_line_to(cr, 0, -h + 2 * r);
  cairo_rel_curve_to(cr, 0.0, -rc, r - rc, -r, r, -r);
  cairo_close_path(cr);
  return shoes_canvas_shape_end(self);
}

VALUE
shoes_canvas_oval(VALUE self, VALUE _x, VALUE _y, VALUE _w, VALUE _h)
{
  double x, y, w, h;
  SETUP();

  x = NUM2DBL(_x);
  y = NUM2DBL(_y);
  w = NUM2DBL(_w);
  h = NUM2DBL(_h);

  shoes_canvas_shape_do(canvas, x + w / 2., y + h / 2.);
  cairo_scale(cr, w / 2., h / 2.);
  cairo_arc(cr, 0., 0., 1., 0., 2 * M_PI);
  return shoes_canvas_shape_end(self);
}

VALUE
shoes_canvas_line(VALUE self, VALUE _x1, VALUE _y1, VALUE _x2, VALUE _y2)
{
  double x, y, x1, y1, x2, y2;
  SETUP();

  x1 = NUM2DBL(_x1);
  y1 = NUM2DBL(_y1);
  x2 = NUM2DBL(_x2);
  y2 = NUM2DBL(_y2);

  x = ((x2 - x1) / 2.) + x1;
  y = ((y2 - y1) / 2.) + y1;
  shoes_canvas_shape_do(canvas, x, y);
  cairo_move_to(cr, x1 - x, y1 - y);
  cairo_line_to(cr, x2 - x, y2 - y);
  return shoes_canvas_shape_end(self);
}

VALUE
shoes_canvas_arrow(VALUE self, VALUE _x, VALUE _y, VALUE _w)
{
  double x, y, w, h, tip;
  SETUP();

  x = NUM2DBL(_x);
  y = NUM2DBL(_y);
  w = NUM2DBL(_w);
  h = w * 0.8;
  tip = w * 0.42;

  shoes_canvas_shape_do(canvas, x - (w / 2.), y);
  cairo_new_path(cr);
  cairo_move_to(cr, w / 2., 0);
  cairo_rel_line_to(cr, -tip, +(h*0.5));
  cairo_rel_line_to(cr, 0, -(h*0.25));
  cairo_rel_line_to(cr, -(w-tip), 0);
  cairo_rel_line_to(cr, 0, -(h*0.5));
  cairo_rel_line_to(cr, +(w-tip), 0);
  cairo_rel_line_to(cr, 0, -(h*0.25));
  cairo_close_path(cr);
  return shoes_canvas_shape_end(self);
}

VALUE
shoes_canvas_star(int argc, VALUE *argv, VALUE self)
{
  VALUE _x, _y, _points, _outer, _inner;
  double x, y, outer, inner, theta;
  int i, points;
  SETUP();

  rb_scan_args(argc, argv, "23", &_x, &_y, &_points, &_outer, &_inner);
  x = NUM2DBL(_x);
  y = NUM2DBL(_y);
  points = 10;
  if (!NIL_P(_points)) points = NUM2INT(_points);
  outer = 100.0;
  if (!NIL_P(_outer)) outer = NUM2DBL(_outer);
  inner = 50.0;
  if (!NIL_P(_inner)) inner = NUM2DBL(_inner);

  theta = (points - 1) * M_PI / (points * 1.);
  shoes_canvas_shape_do(canvas, 0, 0); /* TODO: find star's center */
  cairo_new_path(cr);
  cairo_move_to(cr, x, y);
  for (i = 0; i < points - 1; i++) {
    cairo_rel_line_to(cr, outer, 0);
    cairo_rotate(cr, theta);
  }
  cairo_close_path(cr);
  return shoes_canvas_shape_end(self);
}

VALUE
shoes_canvas_markup(int argc, VALUE *argv, VALUE self)
{
  VALUE msg, attr, text;
  SETUP();

  rb_scan_args(argc, argv, "11", &msg, &attr);
  text = shoes_text_new(msg, attr, self);
  rb_ary_push(canvas->contents, text);
  return text;
}

VALUE
shoes_canvas_imagesize(VALUE self, VALUE _path)
{
  cairo_surface_t *image = cairo_image_surface_create_from_png(RSTRING(_path)->ptr);
  double w = cairo_image_surface_get_width(image);
  double h = cairo_image_surface_get_height(image);
  cairo_surface_destroy(image);
  return rb_ary_new3(2, INT2NUM(w), INT2NUM(h));
}

VALUE
shoes_canvas_background(int argc, VALUE *argv, VALUE self)
{
  VALUE path, attr, pattern;
  SETUP();

  rb_scan_args(argc, argv, "11", &path, &attr);
  pattern = shoes_pattern_new(cBackground, path, attr, self);
  rb_ary_push(canvas->contents, pattern);
  return pattern;
}

VALUE
shoes_canvas_image(int argc, VALUE *argv, VALUE self)
{
  VALUE path, attr, image;
  SETUP();

  rb_scan_args(argc, argv, "11", &path, &attr);
  image = shoes_image_new(cImage, path, attr, self);
  rb_ary_push(canvas->contents, image);
  return image;
}

VALUE
shoes_canvas_path(int argc, VALUE *argv, VALUE self)
{
  VALUE _x, _y;
  double x, y;
  SETUP();

  rb_scan_args(argc, argv, "02", &_x, &_y);

  shoes_canvas_shape_do(canvas, 0, 0); /* TODO: find path center */
  cairo_new_path(cr);
  if (!NIL_P(_x) && !NIL_P(_y))
  {
    x = NUM2DBL(_x);
    y = NUM2DBL(_y);
    cairo_move_to(cr, x, y);
  }
  if (rb_block_given_p())
  {
    rb_yield(Qnil);
  }
  cairo_close_path(cr);
  return shoes_canvas_shape_end(self);
}

VALUE
shoes_canvas_move_to(VALUE self, VALUE _x, VALUE _y)
{
  double x, y;
  SETUP();

  x = NUM2DBL(_x);
  y = NUM2DBL(_y);

  cairo_move_to(cr, x, y);
  return self;
}

VALUE
shoes_canvas_line_to(VALUE self, VALUE _x, VALUE _y)
{
  double x, y;
  SETUP();

  x = NUM2DBL(_x);
  y = NUM2DBL(_y);

  cairo_line_to(cr, x, y);
  return self;
}

VALUE
shoes_canvas_curve_to(VALUE self, VALUE _x1, VALUE _y1, VALUE _x2, VALUE _y2, VALUE _x3, VALUE _y3)
{
  double x1, y1, x2, y2, x3, y3;
  SETUP();

  x1 = NUM2DBL(_x1);
  y1 = NUM2DBL(_y1);
  x2 = NUM2DBL(_x2);
  y2 = NUM2DBL(_y2);
  x3 = NUM2DBL(_x3);
  y3 = NUM2DBL(_y3);

  cairo_curve_to(cr, x1, y1, x2, y2, x3, y3);
  return self;
}

VALUE
shoes_canvas_transform(VALUE self, VALUE _m)
{
  ID m = SYM2ID(_m);
  SETUP();

  if (m == s_center || m == s_corner)
  {
    canvas->mode = m;
  }
  else
  {
    rb_raise(rb_eArgError, "transform must be called with either :center or :corner.");
  }
  return self;
}

VALUE
shoes_canvas_translate(VALUE self, VALUE _x, VALUE _y)
{
  double x, y;
  SETUP();

  x = NUM2DBL(_x);
  y = NUM2DBL(_y);

  cairo_matrix_translate(canvas->tf, x, y);
  return self;
}

VALUE
shoes_canvas_rotate(VALUE self, VALUE _deg)
{
  double rad;
  SETUP();

  rad = NUM2DBL(_deg) * (M_PI / 180);

  cairo_matrix_rotate(canvas->tf, -rad);
  return self;
}

VALUE
shoes_canvas_scale(int argc, VALUE *argv, VALUE self)
{
  VALUE _sx, _sy;
  double sx, sy;
  SETUP();

  rb_scan_args(argc, argv, "11", &_sx, &_sy);

  sx = NUM2DBL(_sx);
  if (NIL_P(_sy)) sy = sx;
  else            sy = NUM2DBL(_sy);

  cairo_matrix_scale(canvas->tf, sx, sy);
  return self;
}

VALUE
shoes_canvas_skew(int argc, VALUE *argv, VALUE self)
{
  cairo_matrix_t matrix;
  VALUE _sx, _sy;
  double sx, sy;
  SETUP();

  rb_scan_args(argc, argv, "11", &_sx, &_sy);

  sx = NUM2DBL(_sx) * (M_PI / 180);
  sy = 0.0;
  if (!NIL_P(_sy)) sy = NUM2DBL(_sy) * (M_PI / 180);

  cairo_matrix_init(&matrix, 1.0, sy, sx, 1.0, 0.0, 0.0);
  cairo_matrix_multiply(canvas->tf, canvas->tf, &matrix);
  return self;
}

VALUE
shoes_canvas_push(VALUE self)
{
  cairo_matrix_t *m;
  SETUP();

  m = canvas->tf;
  if (canvas->grl + 1 > canvas->grt)
  {
    canvas->grt += 8;
    SHOE_REALLOC_N(canvas->gr, cairo_matrix_t, canvas->grt);
  }
  canvas->tf = &canvas->gr[canvas->grl];
  canvas->grl++;
  cairo_matrix_init_identity(canvas->tf);
  cairo_matrix_multiply(canvas->tf, canvas->tf, m);
  return self;
}

VALUE
shoes_canvas_pop(VALUE self)
{
  SETUP();

  if (canvas->grl > 1)
  {
    canvas->grl--;
    canvas->tf = &canvas->gr[canvas->grl - 1];
  }
  return self;
}

VALUE
shoes_canvas_reset(VALUE self)
{
  SETUP();

  cairo_matrix_init_identity(canvas->tf);
  return self;
}

VALUE
shoes_canvas_button(int argc, VALUE *argv, VALUE self)
{
  VALUE text, attr, block, button;
  SETUP();
  rb_scan_args(argc, argv, "11&", &text, &attr, &block);

  if (!NIL_P(block))
    attr = shoes_attr_set(attr, s_click, block);

  button = shoes_control_new(cButton, text, attr, self);
  shoes_button_draw(button, self, attr);
  rb_ary_push(canvas->contents, button);
  return button;
}

VALUE
shoes_canvas_edit_line(int argc, VALUE *argv, VALUE self)
{
  VALUE text, attr, block, edit_line;
  SETUP();
  rb_scan_args(argc, argv, "02&", &text, &attr, &block);

  if (!NIL_P(block))
    attr = shoes_attr_set(attr, s_insert, block);

  edit_line = shoes_control_new(cEditLine, text, attr, self);
  shoes_edit_line_draw(edit_line, self, attr);
  rb_ary_push(canvas->contents, edit_line);
  return edit_line;
}

VALUE
shoes_canvas_edit_box(int argc, VALUE *argv, VALUE self)
{
  VALUE text, attr, block, edit_box;
  SETUP();
  rb_scan_args(argc, argv, "02&", &text, &attr, &block);

  if (!NIL_P(block))
    attr = shoes_attr_set(attr, s_insert, block);

  edit_box = shoes_control_new(cEditBox, text, attr, self);
  shoes_edit_box_draw(edit_box, self, attr);
  rb_ary_push(canvas->contents, edit_box);
  return edit_box;
}

VALUE
shoes_canvas_list_box(int argc, VALUE *argv, VALUE self)
{
  VALUE text, attr, block, list_box;
  SETUP();
  rb_scan_args(argc, argv, "11&", &text, &attr, &block);

  if (!NIL_P(block))
    attr = shoes_attr_set(attr, s_change, block);

  list_box = shoes_control_new(cListBox, text, attr, self);
  shoes_list_box_draw(list_box, self, attr);
  rb_ary_push(canvas->contents, list_box);
  return list_box;
}

VALUE
shoes_canvas_progress(int argc, VALUE *argv, VALUE self)
{
  VALUE text, attr, progress;
  SETUP();
  rb_scan_args(argc, argv, "11", &text, &attr);

  progress = shoes_control_new(cProgress, text, attr, self);
  shoes_progress_draw(progress, self, attr);
  rb_ary_push(canvas->contents, progress);
  return progress;
}

VALUE
shoes_canvas_contents(VALUE self)
{
  shoes_canvas *canvas;
  Data_Get_Struct(self, shoes_canvas, canvas);
  return canvas->contents;
}

void
shoes_canvas_remove_item(VALUE self, VALUE item)
{
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);
#ifndef SHOES_GTK
  long i = rb_ary_index_of(self_t->slot.controls, item);
  if (i >= 0)
    rb_ary_insert_of(self_t->slot.controls, i, 1, Qnil);
#endif
  rb_ary_delete(self_t->contents, item);
}

static void
shoes_canvas_reflow(shoes_canvas *self_t, shoes_canvas *parent)
{
  int inherit = 1;
  VALUE attr = Qnil;
  inherit = DC(self_t->slot) == DC(parent->slot);
  if (inherit) {
    self_t->cr = parent->cr;
    ATTRBASE();
  } else {
    self_t->cr = shoes_cairo_create(&self_t->slot, self_t->width, self_t->height, 20);
  }

  self_t->cx = self_t->x;
  self_t->cy = self_t->y;
  self_t->endx = self_t->x;
  self_t->endy = self_t->y;
  INFO("REFLOW: %0.2f, %0.2f (%0.2f, %0.2f) / %0.2f, %0.2f / %d, %d (%0.2f, %0.2f)\n", self_t->cx, self_t->cy,
    self_t->endx, self_t->endy, self_t->x, self_t->y, self_t->width, self_t->height, parent->cx, parent->cy);
}

VALUE
shoes_canvas_remove(VALUE self)
{
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);
  shoes_canvas_remove_item(self_t->parent, self);
  return self;
}

VALUE
shoes_canvas_draw(VALUE self, VALUE c, VALUE attr)
{
  long i;
  shoes_canvas *self_t;
  shoes_canvas *canvas;
  Data_Get_Struct(self, shoes_canvas, self_t);
  Data_Get_Struct(c, shoes_canvas, canvas);
  INFO("DRAW\n", 0);
  if (self_t->height > self_t->fully)
    self_t->fully = self_t->height;
  if (self_t != canvas)
  {
    shoes_canvas_reflow(self_t, canvas);
#ifdef SHOES_GTK
    self_t->slot.expose = canvas->slot.expose;
#endif
  }
  else
  {
    ATTR_MARGINS(0);
    self_t->x = ATTR2DBL(x, 0) + lmargin;
    self_t->y = ATTR2DBL(y, 0) + tmargin;
    canvas->endx = canvas->cx = self_t->x;
    canvas->endy = canvas->cy = self_t->y;
  }

  if (ATTR(hidden) != Qtrue)
  {
    INFO("CONTENTS: %lu\n", self_t->contents);
    for (i = 0; i < RARRAY_LEN(self_t->contents); i++)
    {
      VALUE ele = rb_ary_entry(self_t->contents, i);
      rb_funcall(ele, s_draw, 2, self, attr);
    }
  }

  canvas->endx = canvas->cx = self_t->x + self_t->width;
  canvas->cy = self_t->cy;
  if (canvas->endy < self_t->endy)
    canvas->endy = self_t->endy;
  self_t->fully = self_t->endy;

#ifdef SHOES_GTK
  self_t->slot.expose = NULL;
#endif

  if (self_t == canvas || DC(self_t->slot) != DC(canvas->slot))
  {
    int endy = (int)self_t->endy;
    if (endy < self_t->height) endy = self_t->height;
#ifdef SHOES_GTK
    gtk_layout_set_size(GTK_LAYOUT(self_t->slot.canvas), self_t->width, self_t->endy);
#endif
#ifdef SHOES_QUARTZ
    HIRect hr;
    HIViewGetFrame(self_t->slot.view, &hr);
    hr.size.width = self_t->width;
    hr.size.height = self_t->endy;
    HIViewSetFrame(self_t->slot.view, &hr);
#endif
  }

  return self;
}

static void
shoes_canvas_memdraw(VALUE self, VALUE block)
{
  SETUP();
  if (canvas->cr != NULL)
    cairo_destroy(canvas->cr);
  canvas->cr = cairo_create(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1));;
  mfp_instance_eval(self, block);
}

static void
shoes_canvas_insert(VALUE self, long i, long mod, VALUE ele, VALUE block)
{
  VALUE ary;
  SETUP();

  if (!NIL_P(ele))
    i = rb_ary_index_of(canvas->contents, ele);
  if (i < 0)
    i += RARRAY_LEN(canvas->contents) + 1;

  ary = canvas->contents;
  canvas->contents = rb_ary_new();
  shoes_canvas_memdraw(self, block);
  rb_ary_insert_at(ary, i + mod, 0, canvas->contents);
  canvas->contents = ary;
}

VALUE
shoes_canvas_after(int argc, VALUE *argv, VALUE self)
{
  VALUE ele, block;
  rb_scan_args(argc, argv, "01&", &ele, &block);
  shoes_canvas_insert(self, -2, 1, ele, block);
  return self;
}

VALUE
shoes_canvas_before(int argc, VALUE *argv, VALUE self)
{
  VALUE ele, block;
  rb_scan_args(argc, argv, "01&", &ele, &block);
  shoes_canvas_insert(self, 0, 0, ele, block);
  return self;
}

VALUE
shoes_canvas_append(int argc, VALUE *argv, VALUE self)
{
  VALUE block;
  rb_scan_args(argc, argv, "0&", &block);
  shoes_canvas_insert(self, -2, 1, Qnil, block);
  return self;
}

VALUE
shoes_canvas_prepend(int argc, VALUE *argv, VALUE self)
{
  VALUE block;
  rb_scan_args(argc, argv, "0&", &block);
  shoes_canvas_insert(self, 0, 0, Qnil, block);
  return self;
}

VALUE
shoes_canvas_clear_contents(int argc, VALUE *argv, VALUE self)
{
  long i;
  VALUE ary, block;
  SETUP();

  rb_scan_args(argc, argv, "0&", &block);
  ary = rb_ary_dup(canvas->contents);
  for (i = 0; i < RARRAY_LEN(ary); i++) 
    shoes_canvas_remove_item(self, rb_ary_entry(ary, i));
  if (!NIL_P(block))
    shoes_canvas_memdraw(self, block);
  shoes_canvas_repaint_all(self);
  return self;
}

VALUE
shoes_canvas_flow(int argc, VALUE *argv, VALUE self)
{
  VALUE attr, block, flow;
  SETUP();

  rb_scan_args(argc, argv, "01&", &attr, &block);
  flow = shoes_flow_new(attr, self);
  mfp_instance_eval(flow, block);
  rb_ary_push(canvas->contents, flow);
  return flow;
}

VALUE
shoes_canvas_stack(int argc, VALUE *argv, VALUE self)
{
  VALUE attr, block, stack;
  SETUP();

  rb_scan_args(argc, argv, "01&", &attr, &block);
  stack = shoes_stack_new(attr, self);
  mfp_instance_eval(stack, block);
  rb_ary_push(canvas->contents, stack);
  return stack;
}

void
shoes_canvas_size(VALUE self, int w, int h)
{
  SETUP();
  canvas->width = w;
  canvas->height = h;
#ifdef SHOES_QUARTZ
  HIRect hr;
  HIViewGetFrame(canvas->slot.scrollview, &hr);
  hr.size.width = canvas->width;
  hr.size.height = canvas->height;
  HIViewSetFrame(canvas->slot.scrollview, &hr);
#endif
}

void
shoes_canvas_repaint_all(VALUE self)
{
  SETUP();
  shoes_slot_repaint(&canvas->slot);
}

VALUE
shoes_canvas_hide(VALUE self)
{
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);
  ATTRSET(hidden, Qtrue);
  shoes_canvas_repaint_all(self);
  return self;
}

VALUE
shoes_canvas_show(VALUE self)
{
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);
  ATTRSET(hidden, Qfalse);
  shoes_canvas_repaint_all(self);
  return self;
}

VALUE
shoes_canvas_toggle(VALUE self)
{
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);
  ATTRSET(hidden, ATTR(hidden) == Qtrue ? Qfalse : Qtrue);
  shoes_canvas_repaint_all(self);
  return self;
}

#define EVENT_HANDLER(x) \
  VALUE \
  shoes_canvas_##x(int argc, VALUE *argv, VALUE self) \
  { \
    VALUE block; \
    SETUP(); \
    rb_scan_args(argc, argv, "0&", &block); \
    canvas->x = block; \
    return self; \
  }

EVENT_HANDLER(click);
EVENT_HANDLER(release);
EVENT_HANDLER(motion);
EVENT_HANDLER(keypress);

static VALUE
shoes_canvas_send_click2(VALUE self, int button, int x, int y)
{
  long i;
  VALUE v = Qnil;
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);

  if (ATTR(hidden) != Qtrue)
  {
    if (!NIL_P(self_t->click))
    {
      shoes_safe_block(self, self_t->click, rb_ary_new3(3, INT2NUM(button), INT2NUM(x), INT2NUM(y)));
    }

    for (i = RARRAY_LEN(self_t->contents) - 1; i >= 0; i--)
    {
      VALUE ele = rb_ary_entry(self_t->contents, i);
      if (rb_obj_is_kind_of(ele, cCanvas))
      {
        v = shoes_canvas_send_click(ele, button, x, y);
      }
      else if (rb_obj_class(ele) == cTextClass)
      {
        v = shoes_text_click(ele, button, x, y);
      }
      if (!NIL_P(v)) return v;
    }
  }

  return Qnil;
}

VALUE
shoes_canvas_goto(VALUE self, VALUE url)
{
  shoes_app_goto(global_app, RSTRING_PTR(url));
  return self;
}

VALUE
shoes_canvas_send_click(VALUE self, int button, int x, int y)
{
  // INFO("click(%d, %d, %d)\n", button, x, y);
  VALUE url = shoes_canvas_send_click2(self, button, x, y);
  if (!NIL_P(url)) shoes_app_goto(global_app, RSTRING_PTR(url));
  return url;
}

void
shoes_canvas_send_release(VALUE self, int button, int x, int y)
{
  long i;
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);
  // INFO("release(%d, %d, %d)\n", button, x, y);

  if (ATTR(hidden) != Qtrue)
  {
    if (!NIL_P(self_t->release))
    {
      shoes_safe_block(self, self_t->release, rb_ary_new3(3, INT2NUM(button), INT2NUM(x), INT2NUM(y)));
    }
  }
}

void
shoes_canvas_send_motion(VALUE self, int x, int y)
{
  long i;
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);
  // INFO("motion(%d, %d)\n", x, y);

  if (ATTR(hidden) != Qtrue)
  {
    if (!NIL_P(self_t->motion))
    {
      shoes_safe_block(self, self_t->motion, rb_ary_new3(2, INT2NUM(x), INT2NUM(y)));
    }
  }
}

void
shoes_canvas_send_keypress(VALUE self, VALUE key)
{
  long i;
  shoes_canvas *self_t;
  Data_Get_Struct(self, shoes_canvas, self_t);

  if (ATTR(hidden) != Qtrue)
  {
    if (!NIL_P(self_t->keypress))
    {
      shoes_safe_block(self, self_t->keypress, rb_ary_new3(1, key));
    }
  }
}

static VALUE
shoes_slot_new(VALUE klass, VALUE attr, VALUE parent)
{
  int margin;
  shoes_canvas *self_t, *pc;
  VALUE self = shoes_canvas_alloc(klass);
  shoes_canvas_clear(self);
  Data_Get_Struct(parent, shoes_canvas, pc);
  Data_Get_Struct(self, shoes_canvas, self_t);
  self_t->cr = pc->cr;
  self_t->slot = pc->slot;
  self_t->parent = parent;
  self_t->attr = attr;
  if (!NIL_P(ATTR(width)) && !NIL_P(ATTR(height))) {
    int x, y, w, h;
    x = ATTR2INT(x, 0);
    y = ATTR2INT(y, 0);
    w = ATTR2INT(width, 100);
    h = ATTR2INT(height, 100);

    shoes_slot_init(self, &pc->slot, w, h);
#ifdef SHOES_GTK
    GtkWidget *slotc = self_t->slot.canvas;
    gtk_layout_put(GTK_LAYOUT(slotc), self_t->slot.box, x, y);
    gtk_widget_show_all(self_t->slot.box);
    self_t->cr = shoes_cairo_create(&self_t->slot, self_t->width, self_t->height, 20);
    self_t->x = self_t->y = 0.0;
    self_t->width = w - 20;
    self_t->height = h - 20;
#endif
  } else {
    shoes_canvas_reflow(self_t, pc);
  }
  return self;
}

//
// Shoes::Flow
//
VALUE
shoes_flow_new(VALUE attr, VALUE parent)
{
  return shoes_slot_new(cFlow, attr, parent);
}

//
// Shoes::Canvas
//
VALUE
shoes_stack_new(VALUE attr, VALUE parent)
{
  return shoes_slot_new(cStack, attr, parent);
}