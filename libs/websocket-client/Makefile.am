noinst_LTLIBRARIES = libwebsocketclient.la

libwebsocketclient_la_SOURCES = src/ws_client.c

libwebsocketclient_la_CPPFLAGS = \
    -I$(top_srcdir)/libs/websocket-client/include \
    -I$(top_srcdir)/libs/apr-toolkit/include \
    $(UNIMRCP_APR_INCLUDES) \
    $(UNIMRCP_APU_INCLUDES)

libwebsocketclient_la_LIBADD = \
    $(top_builddir)/libs/apr-toolkit/libaprtoolkit.la
