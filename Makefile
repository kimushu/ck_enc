
ck_enc: ck_enc.c bmplib.h
	gcc -o $@ -Wall $<

.PHONY: clean
clean:
	rm -f ck_enc

