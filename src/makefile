# Tum araclari tek komutla yonetir (her aracin kendi makefile'ini cagirir).
#   make          -> tum araclar derlenir
#   make install  -> tum araclar proje kokundeki bin/ dizinine kopyalanir
#   make clean    -> tum araclarin make ve make install ciktilari silinir

TOOLS = tiff2raw raw2bmp raw2an2k comp_img \
        segmentation orientation frequency gabor_filter

.PHONY: all install clean

all install clean:
	@for t in $(TOOLS); do $(MAKE) -C $$t $@ || exit 1; done
