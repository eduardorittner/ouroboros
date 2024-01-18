all: main editor

main: main.c
	gcc -O2 -o main main.c

editor: editor.c
	gcc -shared -fPIC -o -O2 editor.so editor.c

clean:
	rm editor.so main
