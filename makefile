CC=gcc
MKDIR_P=mkdir -p
CFLAGS=-Wall -I. `pkg-config --cflags --libs glib-2.0 gobject-2.0 gtk+-3.0 libpeas-1.0 accountsservice libpulse libpulse-mainloop-glib libmutter`

VDE_DATA_DIR=/usr/share/vosdesktop# MUST END WITHOUT SLASH
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
PANEL=vospanel
PANELSRC=$(PANELF)/panel-main.o
PANELOBJS=$(patsubst %.c,%.o,$(PANELSRC))

WMF=session
WM=vossession
WMSRC=$(WMF)/wm.c $(WMF)/background.c
WMINCLUDE=$(patsubst %.c,%.h,$(WMSRC))
WMOBJS=$(patsubst %.c,%.o,$(WMSRC))

all: $(LIBF)/lib$(LIB).so $(PANELF)/$(PANEL).bin $(WMF)/$(WM).bin

test:
	sudo make all install
	$(WM)
	

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

%.lo: %.c
	$(CC) -fPIC -c $(CFLAGS) $< -o $@

%-resource.c:
	glib-compile-resources --sourcedir $(@D) --target $*-resource.c --generate-source $*.gresource.xml
	glib-compile-resources --sourcedir $(@D) --target $*-resource.h --generate-header $*.gresource.xml


install: install_lib install_bins install_data
	
install_data:
	$(MKDIR_P) $(VDE_DATA_DIR)
	cp -r applets $(VDE_DATA_DIR)
	cp $(PANELF)/panel-style.css $(VDE_DATA_DIR)

install_bins:
	cp $(WMF)/$(WM).bin /usr/bin/$(WM)
	cp $(PANELF)/$(PANEL).bin /usr/bin/$(PANEL)
	cp vos.desktop /usr/share/xsessions/vos.desktop

install_lib:
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
	rm -f /usr/lib/lib$(LIB).* /usr/share/gir-1.0/$(GIR).gir /usr/lib/girepository-1.0/$(GIR).typelib /usr/share/xsessions/vos.desktop /usr/bin/$(WM) /usr/bin/$(PANEL)
	
.PHONY: fclean clean install install_lib install_bins install_data uninstall all test