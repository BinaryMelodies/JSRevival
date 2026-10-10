
all: hello.node addon.node create.node read.node func.node

read.node: read.c
	gcc -fPIC -shared $< -o $@ -I/usr/include/node26 -Wl,-undefined,dynamic_lookup -lgmp

%.node: %.c
	gcc -fPIC -shared $< -o $@ -I/usr/include/node26 -Wl,-undefined,dynamic_lookup

clean:
	rm -rf *.node
