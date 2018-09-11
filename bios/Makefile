all: bios.bin

bios.bin: bios.asm keyboard.asm
	nasm -f bin -o $@ $<

clean:
	rm bios.bin
