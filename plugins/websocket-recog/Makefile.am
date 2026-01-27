MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = \
    -I$(top_srcdir)/libs/mrcp-engine/include \
    -I$(top_srcdir)/libs/mrcp/include \
    -I$(top_srcdir)/libs/mrcp/message/include \
    -I$(top_srcdir)/libs/mrcp/control/include \
    -I$(top_srcdir)/libs/mrcp/resources/include \
    -I$(top_srcdir)/libs/mpf/include \
    -I$(top_srcdir)/libs/apr-toolkit/include \
    -I$(top_srcdir)/libs/websocket-client/include \
    $(UNIMRCP_APR_INCLUDES) \
    $(UNIMRCP_APU_INCLUDES)

plugin_LTLIBRARIES = websocketrecog.la

websocketrecog_la_SOURCES = src/websocket_recog_engine.c

websocketrecog_la_LDFLAGS = -module -avoid-version -no-undefined

websocketrecog_la_LIBADD = \
    $(top_builddir)/libs/websocket-client/libwebsocketclient.la

include $(top_srcdir)/build/rules/uniplugin.am
