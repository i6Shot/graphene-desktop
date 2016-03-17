CC=gcc
MKDIR_P=mkdir -p
CFLAGS=-Wall -I. `pkg-config --cflags --libs glib-2.0 gobject-2.0 gtk+-3.0 libpeas-1.0 accountsservice libpulse libpulse-mainloop-glib libmutter`

VDE_DATA_DIR=/usr/share/graphene# MUST END WITHOUT SLASH
CFLAGS+=-DVDE_DATA_DIR=\"$(VDE_DATA_DIR)\"

# libvos
LIBF=lib
LIBSRC=$(LIBF)/panel.c $(LIBF)/applet-extension.c $(LIBF)/system-volume-control.c
VERSION=1.0
NAMESPACE=Vos
SYMBOL_PREFIX=vos
GIR=$(NAMESPACE)-$(VERSION)
LIB=$(SYMBOL_PREFIX)
LIBINCLUDE=$(patsubst %.c,%.h,$(LIBSRC))
LIBOBJS=$(patsubst %.c,%.lo,$(LIBSRC))

# exec
PANELF=panel
PANEL=graphene-panel
PANELSRC=$(PANELF)/panel-main.o
PANELOBJS=$(patsubst %.c,%.o,$(PANELSRC))

WMF=wm
WM=graphene-wm
WMSRC=$(WMF)/wm.c $(WMF)/background.c
WMINCLUDE=$(patsubst %.c,%.h,$(WMSRC))
WMOBJS=$(patsubst %.c,%.o,$(WMSRC))

SESSIONF=session
SESSION=graphene-session
SESSIONSRC=$(SESSIONF)/session.c
SESSIONOBJS=$(patsubst %.c,%.o,$(SESSIONSRC))

all: $(LIBF)/lib$(LIB).so $(PANELF)/$(PANEL).bin $(WMF)/$(WM).bin $(SESSIONF)/$(SESSION).bin

test:
	sudo make all install
	$(SESSION)
	

$(LIBF)/lib$(LIB).so: $(LIBOBJS) $(LIBINCLUDE)
	$(CC) -fPIC -shared $(CFLAGS) $(LIBSRC) -o $@
	
	g-ir-scanner --include GObject-2.0 --include Gtk-3.0 --pkg gtk+-3.0\
		--pkg-export lib$(LIB) --library-path $(shell pwd)/$(LIBF) --no-libtool\
	  --identifier-prefix=$(NAMESPACE) --symbol-prefix=$(SYMBOL_PREFIX) --namespace $(NAMESPACE)\
		--nsversion=$(VERSION) --library $(LIB) --warn-all -o $(LIBF)/$(GIR).gir $(LIBSRC) $(LIBINCLUDE)

	g-ir-compiler $(LIBF)/$(GIR).gir > $(LIBF)/$(GIR).typelib

$(PANELF)/$(PANEL).bin: $(PANELOBJS)
	$(CC) $(CFLAGS) -L/usr/lib -L$(LIBF) -l$(LIB) $^ -o $@

$(WMF)/$(WM).bin: $(WMOBJS) $(WMINCLUDE)
	$(CC) $(CFLAGS) $^ -o $@

$(SESSIONF)/$(SESSION).bin: $(SESSIONOBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.lo: %.c
	$(CC) -fPIC -c $(CFLAGS) $< -o $@

%-resource.c:
	glib-compile-resources --sourcedir $(@D) --target $*-resource.c --generate-source $*.gresource.xml
	glib-compile-resources --sourcedir $(@D) --target $*-resource.h --generate-header $*.gresource.xml


install:
	$(MKDIR_P) $(VDE_DATA_DIR)
	cp -r data/* $(VDE_DATA_DIR)

	cp $(WMF)/$(WM).bin $(VDE_DATA_DIR)/$(WM)
	cp $(PANELF)/$(PANEL).bin $(VDE_DATA_DIR)/$(PANEL)
	cp $(SESSIONF)/$(SESSION).bin /usr/bin/$(SESSION)
	cp graphene.desktop /usr/share/xsessions/graphene.desktop

	cp $(LIBF)/$(GIR).typelib /usr/lib/girepository-1.0/$(GIR).typelib
	cp $(LIBF)/$(GIR).gir /usr/share/gir-1.0/$(GIR).gir
	cp $(LIBF)/lib$(LIB).so /usr/lib/lib$(LIB).so.$(VERSION)
	ln -sf /usr/lib/lib$(LIB).so.$(VERSION) /usr/lib/lib$(LIB).so

fclean: clean uninstall

clean:
	@ # **/ removes from ALL SUBDIRECTORIES!! (Make sure this doesn't clean anything out of ./.git!)
	rm -f **/*.so **/*.gir **/*.typelib **/*.bin **/*.o **/*.lo **/*.gch **/*.o **/*.lo
	rm -f -r tmp-introspect* applets/**/__pycache__
	rm -f $(PANELRES) $(patsubst %.c,%.h,$(PANELRES))
	
uninstall:
	rm -f /usr/lib/lib$(LIB).* /usr/share/gir-1.0/$(GIR).gir /usr/lib/girepository-1.0/$(GIR).typelib /usr/share/xsessions/graphene.desktop /usr/bin/$(SESSION)
	rm -rf $(VDE_DATA_DIR)
	
.PHONY: fclean clean install uninstall all test