INSTALL=install
MANINSTDIR=/usr/local/man/man1
MANPAGE=wmgtemp.1

all:    
	( cd src && $(MAKE) )

depend:    
	( cd src && $(MAKE) depend )

install:    
	( cd src && $(MAKE) install )
	$(INSTALL) -m 755 -c $(MANPAGE) $(MANINSTDIR)/$(MANPAGE)