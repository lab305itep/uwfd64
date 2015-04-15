uwfdtool: uwfdtool.o libvmemap.o uwfd64.o
	g++ $^ -o $@ -lreadline
	
clean:
	-rm *.o uwfdtool
