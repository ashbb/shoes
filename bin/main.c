//
// main.c
// The generic launcher for Shoes.
//
#include <stdio.h>
#include "shoes/app.h"
#include "shoes/internal.h"

#ifdef SHOES_WIN32
int WINAPI
WinMain(HINSTANCE inst, HINSTANCE inst2, LPSTR arg, int style)
#else
int
main(argc, argv)
  int argc;
  char *argv[];
#endif
{
  shoes_init();
  shoes_app *app = shoes_app_new();
  char *uri = NULL;
#ifdef SHOES_WIN32
  app->kit.instance = inst;
  app->kit.style = style;
  app->path = ".";
  if (arg != NULL && strlen(arg) > 0)
    uri = arg;
#else
  app->path = argv[0];
  if (argc > 1)
    uri = argv[1];
#endif
  shoes_app_load(app, uri);
  shoes_app_open(app);
  shoes_app_loop(app, "/");
  shoes_app_close(app);
  shoes_app_free(app);
  return 0;
}