all:    
	( cd src && $(MAKE) )

depend:    
	( cd src && $(MAKE) depend )

install:    
	( cd src && $(MAKE) install INSTDIR=$(INSTDIR))

clean:
	( cd src && $(MAKE) clean)
