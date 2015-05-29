uwfdtool: uwfdtool.o libvmemap.o uwfd64.o log.o
	g++ $^ -o $@ -lreadline -lconfig

uwfd64.o: uwfd64.cpp uwfd64.h libvmemap.h

uwfdtool.o: uwfdtool.cpp uwfd64.h libvmemap.h

libvmemap.o: libvmemap.c libvmemap.h

log.o:	log.c log.h

clean:
	-rm *.o uwfdtool
